#include "electrical_subsolver.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Gaussian elimination with partial pivoting for small dense matrices.
// Solves A*x = b in-place. Throws on singular/near-singular matrix.
void solve_dense_gaussian(float* A, float* b, int N) {
    if (N == 0) return;

    for (int col = 0; col < N; ++col) {
        // Find pivot row
        int pivot_row = col;
        float max_abs = std::fabs(A[col * N + col]);
        for (int row = col + 1; row < N; ++row) {
            float abs_val = std::fabs(A[row * N + col]);
            if (abs_val > max_abs) {
                max_abs = abs_val;
                pivot_row = row;
            }
        }

        // Check singularity
        if (max_abs < 1e-12f) {
            throw std::runtime_error("Singular matrix in electrical solve");
        }

        // Swap rows if needed
        if (pivot_row != col) {
            for (int j = 0; j < N; ++j) {
                std::swap(A[col * N + j], A[pivot_row * N + j]);
            }
            std::swap(b[col], b[pivot_row]);
        }

        // Normalize pivot row
        float pivot = A[col * N + col];
        for (int j = 0; j < N; ++j) {
            A[col * N + j] /= pivot;
        }
        b[col] /= pivot;

        // Eliminate column in other rows
        for (int row = 0; row < N; ++row) {
            if (row == col) continue;
            float factor = A[row * N + col];
            if (std::fabs(factor) < 1e-15f) continue;
            for (int j = 0; j < N; ++j) {
                A[row * N + j] -= factor * A[col * N + j];
            }
            b[row] -= factor * b[col];
        }
    }
}

/// Look up dense index for a signal node within a sorted island_nodes array.
/// Returns -1 if node is not found (should never happen for well-formed islands).
int find_node_index(const std::vector<uint32_t>& island_nodes, uint32_t node) {
    auto it = std::lower_bound(island_nodes.begin(), island_nodes.end(), node);
    if (it != island_nodes.end() && *it == node) {
        return static_cast<int>(it - island_nodes.begin());
    }
    return -1;
}

/// Stamp conductance g between two nodes into the matrix system.
/// Handles all combinations of fixed/unknown nodes.
void stamp_conductance(
    float* A, float* b, int N,
    float g,
    int node_a_idx, int node_b_idx,
    const std::vector<int>& node_to_unknown,
    const std::vector<bool>& is_fixed,
    const std::vector<float>& fixed_voltages
) {
    bool a_fixed = is_fixed[node_a_idx];
    bool b_fixed = is_fixed[node_b_idx];

    if (!a_fixed && !b_fixed) {
        int ia = node_to_unknown[node_a_idx];
        int ib = node_to_unknown[node_b_idx];
        A[ia * N + ia] += g;
        A[ib * N + ib] += g;
        A[ia * N + ib] -= g;
        A[ib * N + ia] -= g;
    } else if (!a_fixed && b_fixed) {
        int ia = node_to_unknown[node_a_idx];
        A[ia * N + ia] += g;
        b[ia] += g * fixed_voltages[node_b_idx];
    } else if (a_fixed && !b_fixed) {
        int ib = node_to_unknown[node_b_idx];
        A[ib * N + ib] += g;
        b[ib] += g * fixed_voltages[node_a_idx];
    }
    // Both fixed: no matrix stamp needed.
}

} // anonymous namespace

void solve_electrical(
    const ElectricalBuildPlan& plan,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    float /*dt*/
) {
    // Find max component_index across all islands to size branch_currents
    uint32_t max_comp_idx = 0;
    bool has_elements = false;
    for (const auto& island : plan.islands) {
        for (const auto& elem : island.elements) {
            has_elements = true;
            max_comp_idx = std::max(max_comp_idx, elem.component_index);
        }
    }

    // Reuse branch_currents capacity across frames: resize keeps old memory,
    // then zero-fill. Avoids reallocation when size is stable frame-to-frame.
    if (has_elements) {
        size_t needed = max_comp_idx + 1;
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

        // Collect fixed nodes and validate no conflicts
        // Reuse scratch buffer for fixed_nodes — resize keeps capacity
        rt.fixed_nodes.clear();
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                rt.fixed_nodes.emplace_back(elem.node_a, elem.value_a);
            }
        }

        // Check for conflicting fixed constraints on same node
        for (size_t i = 0; i < rt.fixed_nodes.size(); ++i) {
            for (size_t j = i + 1; j < rt.fixed_nodes.size(); ++j) {
                if (rt.fixed_nodes[i].first == rt.fixed_nodes[j].first) {
                    if (std::fabs(rt.fixed_nodes[i].second - rt.fixed_nodes[j].second) > 1e-5f) {
                        throw std::runtime_error(
                            "Conflicting fixed voltage constraints on node " +
                            std::to_string(rt.fixed_nodes[i].first) +
                            ": " + std::to_string(rt.fixed_nodes[i].second) +
                            " vs " + std::to_string(rt.fixed_nodes[j].second)
                        );
                    }
                }
            }
        }

        // Build sorted, deduplicated node list for this island
        rt.island_nodes = island.signal_indices;
        std::sort(rt.island_nodes.begin(), rt.island_nodes.end());
        rt.island_nodes.erase(std::unique(rt.island_nodes.begin(), rt.island_nodes.end()), rt.island_nodes.end());

        // Map each node to fixed/unknown status
        size_t node_count = rt.island_nodes.size();
        rt.fixed_voltages.resize(node_count);
        std::fill(rt.fixed_voltages.begin(), rt.fixed_voltages.end(), 0.0f);
        rt.is_fixed.resize(node_count);
        std::fill(rt.is_fixed.begin(), rt.is_fixed.end(), false);

        for (const auto& fn : rt.fixed_nodes) {
            int idx = find_node_index(rt.island_nodes, fn.first);
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
            int node_a_idx = find_node_index(rt.island_nodes, elem.node_a);
            // node_b may be UINT32_MAX for FixedVoltageNode (unused)
            int node_b_idx = (elem.node_b != UINT32_MAX)
                ? find_node_index(rt.island_nodes, elem.node_b) : -1;

            if (node_a_idx == -1 || (elem.node_b != UINT32_MAX && node_b_idx == -1)) {
                throw std::runtime_error("Element references node not in island");
            }

            if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                float g = elem.value_a;
                if (g < 0.0f) {
                    throw std::runtime_error(
                        "Negative conductance " + std::to_string(g) +
                        " on branch component_index " + std::to_string(elem.component_index)
                    );
                }
                stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_voltages);
            }
            else if (elem.kind == ElectricalElementKind::TheveninSource) {
                // Convert Thevenin to Norton: Vth, Rseries -> g=1/R, In=Vth/R
                float Vth = elem.value_a;
                float Rseries = elem.value_b;
                float safe_r = std::max(Rseries, 1e-6f);
                float g = 1.0f / safe_r;
                float In = Vth * g;

                stamp_conductance(A, b, N, g, node_a_idx, node_b_idx,
                                  rt.node_to_unknown, rt.is_fixed, rt.fixed_voltages);

                // Stamp Norton current source: In flows from node_b to node_a.
                // Positive current injected into a node adds to RHS.
                if (!rt.is_fixed[node_a_idx]) {
                    b[rt.node_to_unknown[node_a_idx]] += In;
                }
                if (!rt.is_fixed[node_b_idx]) {
                    b[rt.node_to_unknown[node_b_idx]] -= In;
                }
            }
            // FixedVoltageNode: no matrix stamping (handled via fixed map)
        }

        // Solve linear system if there are unknowns.
        // In editor/runtime we must never hard-crash on singular islands;
        // malformed or underconstrained user graphs are expected during editing.
        bool solve_ok = true;
        if (N > 0) {
            // Phase 5 specialization scaffold: 1x1 dense solve kernel.
            // This is a common island family (single unknown node) and avoids
            // invoking full Gaussian elimination for N==1.
            if (N == 1) {
                rt.counters.solves_n1++;
                float a00 = A[0];
                if (std::fabs(a00) < 1e-12f) {
                    solve_ok = false;
                } else {
                    b[0] /= a00;
                }
            } else if (N == 2) {
                rt.counters.solves_n2++;
                // Phase 5 specialization scaffold: 2x2 dense solve kernel.
                // Solve:
                //   [a00 a01] [x0] = [b0]
                //   [a10 a11] [x1]   [b1]
                // via closed-form inverse with singular guard.
                float a00 = A[0], a01 = A[1];
                float a10 = A[2], a11 = A[3];
                float b0 = b[0], b1 = b[1];
                float det = a00 * a11 - a01 * a10;
                if (std::fabs(det) < 1e-12f) {
                    solve_ok = false;
                } else {
                    b[0] = (b0 * a11 - b1 * a01) / det;
                    b[1] = (a00 * b1 - a10 * b0) / det;
                }
            } else {
                rt.counters.solves_dense++;
                try {
                    solve_dense_gaussian(A, b, N);
                    // After in-place solve, b[] contains the solution vector.
                }
                catch (const std::runtime_error&) {
                    solve_ok = false;
                }
            }
        } else {
            rt.counters.solves_n0++;
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
                if (sig_idx >= st.values.size()) {
                    throw std::runtime_error(
                        "Signal index " + std::to_string(sig_idx) +
                        " out of range (size " + std::to_string(st.values.size()) + ")"
                    );
                }
                rt.island_voltages[i] = st.values[sig_idx];
            }
        }

        // Write voltages back to SimulationState
        for (size_t i = 0; i < node_count; ++i) {
            uint32_t sig_idx = rt.island_nodes[i];
            if (sig_idx >= st.values.size()) {
                throw std::runtime_error(
                    "Signal index " + std::to_string(sig_idx) +
                    " out of range (size " + std::to_string(st.values.size()) + ")"
                );
            }
            st.values[sig_idx] = rt.island_voltages[i];
        }

        // Compute branch currents
        uint32_t worst_branch_component_index = UINT32_MAX;
        float worst_branch_abs_current = -1.0f;
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                rt.branch_currents[elem.component_index] = 0.0f;
                continue;
            }

            int node_a_idx = find_node_index(rt.island_nodes, elem.node_a);
            int node_b_idx = find_node_index(rt.island_nodes, elem.node_b);
            if (node_a_idx == -1 || node_b_idx == -1) {
                throw std::runtime_error("Element references node not in island during current computation");
            }
            float Va = rt.island_voltages[node_a_idx];
            float Vb = rt.island_voltages[node_b_idx];

            float current = 0.0f;
            if (solve_ok) {
                if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                    float g = elem.value_a;
                    current = g * (Va - Vb);
                }
                else if (elem.kind == ElectricalElementKind::TheveninSource) {
                    float Vth = elem.value_a;
                    float Rseries = elem.value_b;
                    float safe_r = std::max(Rseries, 1e-6f);
                    float g = 1.0f / safe_r;
                    float In = Vth * g;
                    // Net Norton branch current, positive from node_a -> node_b.
                    current = g * (Va - Vb) - In;
                }
            }

            rt.branch_currents[elem.component_index] = current;
            float abs_i = std::abs(current);
            if (abs_i > worst_branch_abs_current) {
                worst_branch_abs_current = abs_i;
                worst_branch_component_index = elem.component_index;
            }
        }

        // KCL residual diagnostics per node: sum of currents entering node.
        rt.kcl_residuals.resize(node_count);
        std::fill(rt.kcl_residuals.begin(), rt.kcl_residuals.end(), 0.0f);
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
                continue;
            }

            int node_a_idx = find_node_index(rt.island_nodes, elem.node_a);
            int node_b_idx = find_node_index(rt.island_nodes, elem.node_b);
            if (node_a_idx == -1 || node_b_idx == -1) {
                continue;
            }

            float Va = rt.island_voltages[node_a_idx];
            float Vb = rt.island_voltages[node_b_idx];
            float i_ab = 0.0f;
            if (elem.kind == ElectricalElementKind::ConductanceBranch) {
                float g = elem.value_a;
                i_ab = g * (Va - Vb);
            } else if (elem.kind == ElectricalElementKind::TheveninSource) {
                float Vth = elem.value_a;
                float Rseries = elem.value_b;
                float safe_r = std::max(Rseries, 1e-6f);
                float g = 1.0f / safe_r;
                float In = Vth * g;
                i_ab = g * (Va - Vb) - In;
            }

            // Positive i_ab means leaving node_a and entering node_b.
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
            worst_branch_component_index
        });
    }
}
