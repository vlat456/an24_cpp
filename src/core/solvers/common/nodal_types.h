#pragma once

/// Domain-agnostic nodal analysis types.
///
/// All nodal domains (electrical, hydraulic, pneumatic) use the same
/// three-element model: FixedNode (boundary), Source (Thevenin/Norton
/// equivalent), Branch (conductance). The solver operates on abstract
/// "potentials" and "flows" — units are enforced by the build pipeline's
/// extractors, not the solver.
///
/// Terminology mapping:
///   Electrical:  potential = voltage,  flow = current
///   Hydraulic:   potential = pressure, flow = volumetric flow rate
///   Pneumatic:   potential = pressure, flow = volumetric flow rate (incompressible approx.)

#include <cstdint>
#include <vector>

// == NodalElementKind ========================================================
// The only element kinds the subsolver sees. Domain-specific expansions
// (e.g., KnobSwitchBranches → N ConductanceBranch elements) happen at build
// time in the extractor pipeline, before the subsolver runs.
enum class NodalElementKind : uint8_t {
    FixedNode,  ///< Boundary condition (e.g., RefNode, PressureRef)
    Source,     ///< Thevenin equivalent: value_a=source, value_b=R_internal
    Branch,     ///< Conductance between two nodes: value_a=g
};

// == NodalElement =============================================================
struct NodalElement {
    NodalElementKind kind;
    uint32_t node_a;
    uint32_t node_b;       ///< UINT32_MAX for FixedNode (single-node)
    float value_a;         ///< source potential (Source) or conductance (Branch)
    float value_b;         ///< internal resistance (Source only); unused otherwise
    uint32_t element_id;   ///< index into branch_flows for post-solve readout
};

// == NodalIslandPlan ==========================================================
struct NodalIslandPlan {
    std::vector<uint32_t> signal_indices;
    std::vector<NodalElement> elements;
};

// == NodalBuildPlan ===========================================================
struct NodalBuildPlan {
    std::vector<NodalIslandPlan> islands;
};

// == NodalPrimitiveHandle =====================================================
// Identifies a specific element within an island. Components keep their
// domain-specific member names (electrical_handle, hydraulic_handle) but the
// type is unified.
struct NodalPrimitiveHandle {
    uint32_t island_index = UINT32_MAX;
    uint32_t element_index = UINT32_MAX;
    uint32_t element_id = UINT32_MAX;
};

/// Check if a handle points to a valid element.
inline bool is_valid(const NodalPrimitiveHandle& h) {
    return h.island_index != UINT32_MAX && h.element_index != UINT32_MAX;
}

// == NodalRuntimeState ========================================================
// Scratch buffers for nodal solve. Sized to max island node count.
// All vectors retain capacity across frames — resize() keeps existing memory.
// Use reserve() at init time to pre-allocate to max island size.
struct NodalRuntimeState {
    /// Enable to collect per-island KCL residual diagnostics each frame.
    /// Disabled by default in production — significant per-frame cost.
    bool enable_diagnostics = false;

    struct SolveCounters {
        uint32_t islands_total = 0;
        uint32_t solves_n0 = 0;
        uint32_t solves_n1 = 0;
        uint32_t solves_n2 = 0;
        uint32_t solves_dense = 0;
        uint32_t singular_fallbacks = 0;
    };

    struct IslandDiagnostic {
        uint32_t island_index = 0;
        bool solve_ok = true;
        uint32_t unknown_count = 0;
        uint32_t worst_node_signal = UINT32_MAX;
        float worst_node_potential = 0.0f;
        float max_abs_kcl_residual = 0.0f;
        uint32_t worst_branch_element_id = UINT32_MAX;
    };

    std::vector<float> branch_flows;       ///< Solved flow through each element
    std::vector<float> element_value_a;    ///< Dynamic source values (patched each frame)
    std::vector<float> scratch_matrix;
    std::vector<float> scratch_rhs;

    std::vector<uint32_t> island_nodes;
    std::vector<std::pair<uint32_t, float>> fixed_nodes;
    std::vector<float> fixed_potentials;   ///< Boundary condition values
    std::vector<uint8_t> is_fixed;         ///< uint8_t, NOT bool — avoids bit-packed vector
    std::vector<int> node_to_unknown;
    std::vector<float> island_potentials;  ///< Solved node potentials
    std::vector<float> kcl_residuals;

    std::vector<IslandDiagnostic> island_diagnostics;
    SolveCounters counters;

    void reset_counters() {
        counters = {};
    }

    void reserve(uint32_t max_nodes, uint32_t max_elements, uint32_t max_element_id) {
        uint32_t max_unknowns = max_nodes;
        branch_flows.reserve(max_element_id + 1);
        element_value_a.reserve(max_element_id + 1);
        island_nodes.reserve(max_nodes);
        fixed_nodes.reserve(max_elements);
        fixed_potentials.reserve(max_nodes);
        is_fixed.reserve(max_nodes);
        node_to_unknown.reserve(max_nodes);
        island_potentials.reserve(max_nodes);
        kcl_residuals.reserve(max_nodes);
        scratch_matrix.reserve(static_cast<size_t>(max_unknowns) * max_unknowns);
        scratch_rhs.reserve(max_unknowns);
    }
};

/// Get the solved branch flow for a given handle.
/// Returns 0.0f for invalid handles or out-of-range indices.
/// Safe to call from component runtime code.
inline float get_branch_flow(const NodalRuntimeState& rt, const NodalPrimitiveHandle& handle) {
    if (!is_valid(handle)) {
        return 0.0f;
    }
    if (handle.element_id >= rt.branch_flows.size()) {
        return 0.0f;
    }
    return rt.branch_flows[handle.element_id];
}
