#include "nodal_subsolver.h"
#include "nodal_core.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

void solve_nodal(
    const NodalBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    NodalRuntimeState& rt,
    double /*dt*/
) noexcept {
    // Find max element_id across all islands to size branch_flows
    uint32_t max_element_id = 0;
    bool has_elements = false;
    for (const auto& island : plan.islands) {
        for (const auto& elem : island.elements) {
            has_elements = true;
            max_element_id = std::max(max_element_id, elem.element_id);
        }
    }

    // Reuse branch_flows capacity across frames: resize keeps old memory,
    // then zero-fill. Avoids reallocation when size is stable frame-to-frame.
    if (has_elements) {
        size_t needed = max_element_id + 1;
        rt.branch_flows.resize(needed);
        std::memset(rt.branch_flows.data(), 0, needed * sizeof(float));
    } else {
        rt.branch_flows.clear();
    }

    rt.island_diagnostics.clear();
    rt.reset_counters();
    rt.counters.islands_total = static_cast<uint32_t>(plan.islands.size());

    // Process each island independently
    for (size_t island_idx = 0; island_idx < plan.islands.size(); ++island_idx) {
        const auto& island = plan.islands[island_idx];
        auto value_a_for = [&](const NodalElement& elem) -> float {
            if (elem.element_id < element_value_a.size()) {
                return element_value_a[elem.element_id];
            }
            return elem.value_a;
        };

        // Collect fixed nodes and validate no conflicts
        rt.fixed_nodes.clear();
        for (const auto& elem : island.elements) {
            if (elem.kind == NodalElementKind::FixedNode) {
                rt.fixed_nodes.emplace_back(elem.node_a, value_a_for(elem));
            }
        }

        // Check for conflicting fixed constraints on same node.
        // Debug assert on conflict; release: silently use first value.
        for (size_t i = 0; i < rt.fixed_nodes.size(); ++i) {
            for (size_t j = i + 1; j < rt.fixed_nodes.size(); ++j) {
                if (rt.fixed_nodes[i].first == rt.fixed_nodes[j].first) {
                    assert(std::fabs(rt.fixed_nodes[i].second - rt.fixed_nodes[j].second) <= 1e-5f
                           && "Conflicting fixed node constraints on same node");
                    // Release: silently deduplicate — use first value.
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
        rt.fixed_potentials.resize(node_count);
        std::fill(rt.fixed_potentials.begin(), rt.fixed_potentials.end(), 0.0f);
        rt.is_fixed.resize(node_count);
        std::fill(rt.is_fixed.begin(), rt.is_fixed.end(), false);

        for (const auto& fn : rt.fixed_nodes) {
            int idx = nodal::find_node_index(rt.island_nodes, fn.first);
            if (idx >= 0) {
                rt.fixed_potentials[idx] = fn.second;
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
            // node_b may be UINT32_MAX for FixedNode (unused)
            int node_b_idx = (elem.node_b != UINT32_MAX)
                ? nodal::find_node_index(rt.island_nodes, elem.node_b) : -1;

            if (node_a_idx == -1 || (elem.node_b != UINT32_MAX && node_b_idx == -1)) {
                assert(false && "Element references node not in island");
                continue;  // Release: skip malformed element
            }

            if (elem.kind == NodalElementKind::Branch) {
                float g = value_a_for(elem);
                if (g < 0.0f) {
                    assert(false && "Negative conductance");
                    g = 0.0f;  // Release: clamp to zero (open circuit)
                }
                nodal::stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_potentials);
            }
            else if (elem.kind == NodalElementKind::Source) {
                // Convert Thevenin to Norton: source_val, R → g=1/R, In=source_val/R
                float source_val = value_a_for(elem);
                float R = elem.value_b;
                float safe_r = std::max(R, 1e-6f);
                float g = 1.0f / safe_r;
                float In = source_val * g;

                nodal::stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_potentials);

                // Stamp Norton current source via shared utility.
                nodal::stamp_norton_source(b, In, node_a_idx, node_b_idx,
                                           rt.node_to_unknown, rt.is_fixed);
            }
            // FixedNode: no matrix stamping (handled via fixed map)
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

        // Build complete potential map and write back to SimulationState
        rt.island_potentials.resize(node_count);
        std::fill(rt.island_potentials.begin(), rt.island_potentials.end(), 0.0f);
        for (size_t i = 0; i < node_count; ++i) {
            if (rt.is_fixed[i]) {
                rt.island_potentials[i] = rt.fixed_potentials[i];
            } else if (solve_ok) {
                rt.island_potentials[i] = b[rt.node_to_unknown[i]];
            } else {
                // Singular island fallback: preserve previous state value.
                // This keeps the simulation stable and avoids aborting editor runtime.
                uint32_t sig_idx = rt.island_nodes[i];
                assert(sig_idx < st.values.size() && "Signal index out of range");
                if (sig_idx < st.values.size()) {
                    rt.island_potentials[i] = st.values[sig_idx];
                } else {
                    rt.island_potentials[i] = 0.0f;  // Release: safe default
                }
            }
        }

        // Write potentials back to SimulationState
        for (size_t i = 0; i < node_count; ++i) {
            uint32_t sig_idx = rt.island_nodes[i];
            assert(sig_idx < st.values.size() && "Signal index out of range");
            if (sig_idx >= st.values.size()) continue;  // Release: skip OOB
            st.values[sig_idx] = rt.island_potentials[i];
        }

        // Compute branch flows
        uint32_t worst_branch_element_id = UINT32_MAX;
        float worst_branch_abs_flow = -1.0f;
        for (const auto& elem : island.elements) {
            if (elem.kind == NodalElementKind::FixedNode) {
                rt.branch_flows[elem.element_id] = 0.0f;
                continue;
            }

            int node_a_idx = nodal::find_node_index(rt.island_nodes, elem.node_a);
            int node_b_idx = nodal::find_node_index(rt.island_nodes, elem.node_b);
            if (node_a_idx == -1 || node_b_idx == -1) {
                assert(false && "Element references node not in island during flow computation");
                continue;  // Release: skip malformed element
            }
            float Pa = rt.island_potentials[node_a_idx];
            float Pb = rt.island_potentials[node_b_idx];

            float flow = 0.0f;
            if (solve_ok) {
                if (elem.kind == NodalElementKind::Branch) {
                    float g = value_a_for(elem);
                    flow = g * (Pa - Pb);
                }
                else if (elem.kind == NodalElementKind::Source) {
                    float source_val = value_a_for(elem);
                    float R = elem.value_b;
                    float safe_r = std::max(R, 1e-6f);
                    float g = 1.0f / safe_r;
                    float In = source_val * g;
                    // Net Norton branch flow, positive from node_a -> node_b.
                    flow = g * (Pa - Pb) - In;
                }
            }

            rt.branch_flows[elem.element_id] = flow;
            float abs_f = std::abs(flow);
            if (abs_f > worst_branch_abs_flow) {
                worst_branch_abs_flow = abs_f;
                worst_branch_element_id = elem.element_id;
            }
        }

        // KCL residual diagnostics — expensive, only when explicitly enabled.
        if (rt.enable_diagnostics) {
            rt.kcl_residuals.resize(node_count);
            std::fill(rt.kcl_residuals.begin(), rt.kcl_residuals.end(), 0.0f);
            for (const auto& elem : island.elements) {
                if (elem.kind == NodalElementKind::FixedNode) {
                    continue;
                }

                int node_a_idx = nodal::find_node_index(rt.island_nodes, elem.node_a);
                int node_b_idx = nodal::find_node_index(rt.island_nodes, elem.node_b);
                if (node_a_idx == -1 || node_b_idx == -1) {
                    continue;
                }

                float Pa = rt.island_potentials[node_a_idx];
                float Pb = rt.island_potentials[node_b_idx];
                float f_ab = 0.0f;
                if (elem.kind == NodalElementKind::Branch) {
                    float g = value_a_for(elem);
                    f_ab = g * (Pa - Pb);
                } else if (elem.kind == NodalElementKind::Source) {
                    float source_val = value_a_for(elem);
                    float R = elem.value_b;
                    float safe_r = std::max(R, 1e-6f);
                    float g = 1.0f / safe_r;
                    float In = source_val * g;
                    f_ab = g * (Pa - Pb) - In;
                }

                rt.kcl_residuals[node_a_idx] -= f_ab;
                rt.kcl_residuals[node_b_idx] += f_ab;
            }

            float max_abs_residual = 0.0f;
            uint32_t worst_signal = UINT32_MAX;
            float worst_potential = 0.0f;
            for (size_t i = 0; i < node_count; ++i) {
                float ar = std::abs(rt.kcl_residuals[i]);
                if (ar > max_abs_residual) {
                    max_abs_residual = ar;
                    worst_signal = rt.island_nodes[i];
                    worst_potential = rt.island_potentials[i];
                }
            }

            rt.island_diagnostics.push_back({
                static_cast<uint32_t>(island_idx),
                solve_ok,
                static_cast<uint32_t>(N),
                worst_signal,
                worst_potential,
                max_abs_residual,
                worst_branch_element_id
            });
        }
    }
}

void solve_nodal(
    const NodalBuildPlan& plan,
    SimulationState& st,
    NodalRuntimeState& rt,
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

    solve_nodal(plan, rt.element_value_a, st, rt, dt);
}
