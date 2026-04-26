#include "electrical_subsolver.h"
#include "nodal_core.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

void solve_electrical(
    const ElectricalBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    double /*dt*/
) noexcept {
    // Find max element_id across all islands to size branch_currents
    uint32_t max_element_id = 0;
    bool has_elements = false;
    for (const auto& island : plan.islands) {
        for (const auto& elem : island.elements) {
            has_elements = true;
            max_element_id = std::max(max_element_id, elem.element_id);
        }
    }

    // Reuse branch_currents capacity across frames: resize keeps old memory,
    // then zero-fill. Avoids reallocation when size is stable frame-to-frame.
    if (has_elements) {
        size_t needed = max_element_id + 1;
        rt.branch_currents.resize(needed);
        std::memset(rt.branch_currents.data(), 0, needed * sizeof(float));
    } else {
        rt.branch_currents.clear();
    }

    rt.island_diagnostics.clear();
    rt.reset_counters();
    rt.counters.islands_total = static_cast<uint32_t>(plan.islands.size());

    // Process each island independently
    for (size_t island_idx = 0; island_idx < plan.islands.size(); ++island_idx) {
        const auto& island = plan.islands[island_idx];
        auto value_a_for = [&](const ElectricalElement& elem) -> float {
            if (elem.element_id < element_value_a.size()) {
                return element_value_a[elem.element_id];
            }
            return elem.value_a;
        };

        // Collect fixed nodes and validate no conflicts
        // Reuse scratch buffer for fixed_nodes — resize keeps capacity
        rt.fixed_nodes.clear();
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                rt.fixed_nodes.emplace_back(elem.node_a, value_a_for(elem));
            }
        }

        // Check for conflicting fixed constraints on same node.
        // Debug assert on conflict; release: silently use first value (safe for game).
        for (size_t i = 0; i < rt.fixed_nodes.size(); ++i) {
            for (size_t j = i + 1; j < rt.fixed_nodes.size(); ++j) {
                if (rt.fixed_nodes[i].first == rt.fixed_nodes[j].first) {
                    assert(std::fabs(rt.fixed_nodes[i].second - rt.fixed_nodes[j].second) <= 1e-5f
                           && "Conflicting fixed voltage constraints on same node");
                    // Release: silently deduplicate — use first value.
                    // Remove the duplicate so it doesn't affect later processing.
                    rt.fixed_nodes.erase(rt.fixed_nodes.begin() + static_cast<ptrdiff_t>(j));
                    --j;
                }
            }
        }

        // Use island's pre-sorted, pre-deduplicated signal indices directly.
        // signal_indices are built from std::set at build time — already sorted and unique.
        const auto& island_nodes_ref = island.signal_indices;
        rt.island_nodes.assign(island_nodes_ref.begin(), island_nodes_ref.end());

        // Map each node to fixed/unknown status
        size_t node_count = rt.island_nodes.size();
        rt.fixed_voltages.resize(node_count);
        std::fill(rt.fixed_voltages.begin(), rt.fixed_voltages.end(), 0.0f);
        rt.is_fixed.resize(node_count);
        std::fill(rt.is_fixed.begin(), rt.is_fixed.end(), false);

        for (const auto& fn : rt.fixed_nodes) {
            int idx = nodal::find_node_index(rt.island_nodes, fn.first);
            if (idx >= 0) {
                rt.fixed_voltages[idx] = fn.second;
                rt.is_fixed[idx] = true;
            }
        }

        // Count unknown nodes and build dense index mapping
        int N = 0;
        rt.node_to_unknown.resize(node_count);
        std::fill(rt.node_to_unknown.begin(), rt.node_to_unknown.end(), -1);
        for (size_t i = 0; i < node_count; ++i) {
            if (!rt.is_fixed[i]) {
                rt.node_to_unknown[i] = N++;
            }
        }

        // Use scratch buffers from rt to avoid per-frame allocation for matrix A and rhs b.
        // These persist across frames so resize (not assign) keeps capacity stable.
        rt.scratch_matrix.resize(static_cast<size_t>(N) * N);
        std::memset(rt.scratch_matrix.data(), 0, static_cast<size_t>(N) * N * sizeof(float));
        rt.scratch_rhs.resize(N);
        std::memset(rt.scratch_rhs.data(), 0, static_cast<size_t>(N) * sizeof(float));

        float* A = rt.scratch_matrix.data();
        float* b = rt.scratch_rhs.data();

        // Stamp all elements into conductance matrix
        for (const auto& elem : island.elements) {
            int node_a_idx = nodal::find_node_index(rt.island_nodes, elem.node_a);
            // node_b may be UINT32_MAX for FixedVoltageNode (unused)
            int node_b_idx = (elem.node_b != UINT32_MAX)
                ? nodal::find_node_index(rt.island_nodes, elem.node_b) : -1;

            if (node_a_idx == -1 || (elem.node_b != UINT32_MAX && node_b_idx == -1)) {
                assert(false && "Element references node not in island");
                continue;  // Release: skip malformed element
            }

            if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                float g = value_a_for(elem);
                if (g < 0.0f) {
                    assert(false && "Negative conductance");
                    g = 0.0f;  // Release: clamp to zero (open circuit)
                }
                nodal::stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_voltages);
            }
            else if (elem.kind == ElectricalElementKind::TheveninSource) {
                // Convert Thevenin to Norton: Vth, Rseries -> g=1/R, In=Vth/R
                float Vth = value_a_for(elem);
                float Rseries = elem.value_b;
                float safe_r = std::max(Rseries, 1e-6f);
                float g = 1.0f / safe_r;
                float In = Vth * g;

                nodal::stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_voltages);

                // Stamp Norton current source via shared utility.
                nodal::stamp_norton_source(b, In, node_a_idx, node_b_idx,
                                           rt.node_to_unknown, rt.is_fixed);
            }
            // FixedVoltageNode: no matrix stamping (handled via fixed map)
        }

        // Solve linear system if there are unknowns.
        // In editor/runtime we must never hard-crash on singular islands;
        // malformed or underconstrained user graphs are expected during editing.
        auto [solve_ok, category] = nodal::solve_linear_system(A, b, N);
        switch (category) {
            case nodal::SolveCategory::N0: rt.counters.solves_n0++; break;
            case nodal::SolveCategory::N1: rt.counters.solves_n1++; break;
            case nodal::SolveCategory::N2: rt.counters.solves_n2++; break;
            case nodal::SolveCategory::Dense: rt.counters.solves_dense++; break;
        }

        if (!solve_ok) {
            rt.counters.singular_fallbacks++;
        }

        // Build complete voltage map and write back to SimulationState
        rt.island_voltages.resize(node_count);
        std::fill(rt.island_voltages.begin(), rt.island_voltages.end(), 0.0f);
        for (size_t i = 0; i < node_count; ++i) {
            if (rt.is_fixed[i]) {
                rt.island_voltages[i] = rt.fixed_voltages[i];
            } else if (solve_ok) {
                rt.island_voltages[i] = b[rt.node_to_unknown[i]];
            } else {
                // Singular island fallback: preserve previous state value.
                // This keeps the simulation stable and avoids aborting editor runtime.
                uint32_t sig_idx = rt.island_nodes[i];
                assert(sig_idx < st.values.size() && "Signal index out of range");
                if (sig_idx < st.values.size()) {
                    rt.island_voltages[i] = st.values[sig_idx];
                } else {
                    rt.island_voltages[i] = 0.0f;  // Release: safe default
                }
            }
        }

        // Write voltages back to SimulationState
        for (size_t i = 0; i < node_count; ++i) {
            uint32_t sig_idx = rt.island_nodes[i];
            assert(sig_idx < st.values.size() && "Signal index out of range");
            if (sig_idx >= st.values.size()) continue;  // Release: skip OOB
            st.values[sig_idx] = rt.island_voltages[i];
        }

        // Compute branch currents
        uint32_t worst_branch_element_id = UINT32_MAX;
        float worst_branch_abs_current = -1.0f;
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                rt.branch_currents[elem.element_id] = 0.0f;
                continue;
            }

            int node_a_idx = nodal::find_node_index(rt.island_nodes, elem.node_a);
            int node_b_idx = nodal::find_node_index(rt.island_nodes, elem.node_b);
            if (node_a_idx == -1 || node_b_idx == -1) {
                assert(false && "Element references node not in island during current computation");
                continue;  // Release: skip malformed element
            }
            float Va = rt.island_voltages[node_a_idx];
            float Vb = rt.island_voltages[node_b_idx];

            float current = 0.0f;
            if (solve_ok) {
                if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                    float g = value_a_for(elem);
                    current = g * (Va - Vb);
                }
                else if (elem.kind == ElectricalElementKind::TheveninSource) {
                    float Vth = value_a_for(elem);
                    float Rseries = elem.value_b;
                    float safe_r = std::max(Rseries, 1e-6f);
                    float g = 1.0f / safe_r;
                    float In = Vth * g;
                    // Net Norton branch current, positive from node_a -> node_b.
                    current = g * (Va - Vb) - In;
                }
            }

            rt.branch_currents[elem.element_id] = current;
            float abs_i = std::abs(current);
            if (abs_i > worst_branch_abs_current) {
                worst_branch_abs_current = abs_i;
                worst_branch_element_id = elem.element_id;
            }
        }

        // KCL residual diagnostics — expensive, only when explicitly enabled.
        if (rt.enable_diagnostics) {
            rt.kcl_residuals.resize(node_count);
            std::fill(rt.kcl_residuals.begin(), rt.kcl_residuals.end(), 0.0f);
            for (const auto& elem : island.elements) {
                if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                    continue;
                }

                int node_a_idx = nodal::find_node_index(rt.island_nodes, elem.node_a);
                int node_b_idx = nodal::find_node_index(rt.island_nodes, elem.node_b);
                if (node_a_idx == -1 || node_b_idx == -1) {
                    continue;
                }

                float Va = rt.island_voltages[node_a_idx];
                float Vb = rt.island_voltages[node_b_idx];
                float i_ab = 0.0f;
                if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                    float g = value_a_for(elem);
                    i_ab = g * (Va - Vb);
                } else if (elem.kind == ElectricalElementKind::TheveninSource) {
                    float Vth = value_a_for(elem);
                    float Rseries = elem.value_b;
                    float safe_r = std::max(Rseries, 1e-6f);
                    float g = 1.0f / safe_r;
                    float In = Vth * g;
                    i_ab = g * (Va - Vb) - In;
                }

                rt.kcl_residuals[node_a_idx] -= i_ab;
                rt.kcl_residuals[node_b_idx] += i_ab;
            }

            float max_abs_residual = 0.0f;
            uint32_t worst_signal = UINT32_MAX;
            float worst_voltage = 0.0f;
            for (size_t i = 0; i < node_count; ++i) {
                float ar = std::abs(rt.kcl_residuals[i]);
                if (ar > max_abs_residual) {
                    max_abs_residual = ar;
                    worst_signal = rt.island_nodes[i];
                    worst_voltage = rt.island_voltages[i];
                }
            }

            rt.island_diagnostics.push_back({
                static_cast<uint32_t>(island_idx),
                solve_ok,
                static_cast<uint32_t>(N),
                worst_signal,
                worst_voltage,
                max_abs_residual,
                worst_branch_element_id
            });
        }
    }
}

void solve_electrical(
    const ElectricalBuildPlan& plan,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    double dt
) noexcept {
    uint32_t max_element_id = 0;
    bool has_elements = false;
    for (const auto& island : plan.islands) {
        for (const auto& elem : island.elements) {
            has_elements = true;
            max_element_id = std::max(max_element_id, elem.element_id);
        }
    }

    if (has_elements) {
        const size_t needed = static_cast<size_t>(max_element_id) + 1;
        if (rt.element_value_a.size() < needed) {
            rt.element_value_a.resize(needed, 0.0f);
            for (const auto& island : plan.islands) {
                for (const auto& elem : island.elements) {
                    if (elem.element_id < rt.element_value_a.size()) {
                        rt.element_value_a[elem.element_id] = elem.value_a;
                    }
                }
            }
        }
    } else {
        rt.element_value_a.clear();
    }

    solve_electrical(plan, rt.element_value_a, st, rt, dt);
}
