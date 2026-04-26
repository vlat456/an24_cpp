#pragma once

/// Nodal analysis core — domain-agnostic solver utilities.
///
/// Shared by electrical and hydraulic subsolvers. The nodal analysis algorithm
/// (conductance stamping, Gaussian elimination, branch flow computation) is
/// identical across domains. Only the element kind dispatch differs.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace nodal {

/// Gaussian elimination with partial pivoting for small dense matrices.
/// Solves A*x = b in-place. Returns false on singular/near-singular matrix.
[[nodiscard]] inline bool solve_dense_gaussian(float* A, float* b, int N) noexcept {
    if (N == 0) return true;

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
            return false;
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
    return true;
}

/// Binary search for node index in a sorted island_nodes array.
/// Returns -1 if node is not found.
inline int find_node_index(const std::vector<uint32_t>& island_nodes, uint32_t node) {
    auto it = std::lower_bound(island_nodes.begin(), island_nodes.end(), node);
    if (it != island_nodes.end() && *it == node) {
        return static_cast<int>(it - island_nodes.begin());
    }
    return -1;
}

/// Stamp conductance g between two nodes into the matrix system.
/// Handles all combinations of fixed/unknown nodes.
inline void stamp_conductance(
    float* A, float* b, int N,
    float g,
    int node_a_idx, int node_b_idx,
    const std::vector<int>& node_to_unknown,
    const std::vector<uint8_t>& is_fixed,
    const std::vector<float>& fixed_values
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
        b[ia] += g * fixed_values[node_b_idx];
    } else if (a_fixed && !b_fixed) {
        int ib = node_to_unknown[node_b_idx];
        A[ib * N + ib] += g;
        b[ib] += g * fixed_values[node_a_idx];
    }
    // Both fixed: no matrix stamp needed.
}

/// Solve category for counter tracking. Returned by solve_linear_system().
enum class SolveCategory { N0, N1, N2, Dense };

/// Unified solve dispatch: picks N=0/1/2/dense specialization.
/// Solves A*x = b in-place. Returns {ok, category}.
[[nodiscard]] inline std::pair<bool, SolveCategory> solve_linear_system(float* A, float* b, int N) noexcept {
    if (N == 0) return {true, SolveCategory::N0};

    if (N == 1) {
        if (std::fabs(A[0]) < 1e-12f) return {false, SolveCategory::N1};
        b[0] /= A[0];
        return {true, SolveCategory::N1};
    }

    if (N == 2) {
        float a00 = A[0], a01 = A[1];
        float a10 = A[2], a11 = A[3];
        float det = a00 * a11 - a01 * a10;
        if (std::fabs(det) < 1e-12f) return {false, SolveCategory::N2};
        float b0 = b[0], b1 = b[1];
        b[0] = (b0 * a11 - b1 * a01) / det;
        b[1] = (a00 * b1 - a10 * b0) / det;
        return {true, SolveCategory::N2};
    }

    bool ok = solve_dense_gaussian(A, b, N);
    return {ok, SolveCategory::Dense};
}

/// Stamp Norton-equivalent source into RHS vector.
/// Source is injected into node_a (+source) and extracted from node_b (-source),
/// representing current/flow flowing from b to a through the source.
inline void stamp_norton_source(
    float* b,
    float source,
    int node_a_idx,
    int node_b_idx,
    const std::vector<int>& node_to_unknown,
    const std::vector<uint8_t>& is_fixed
) {
    if (!is_fixed[node_a_idx]) {
        b[node_to_unknown[node_a_idx]] += source;
    }
    if (!is_fixed[node_b_idx]) {
        b[node_to_unknown[node_b_idx]] -= source;
    }
}

} // namespace nodal
