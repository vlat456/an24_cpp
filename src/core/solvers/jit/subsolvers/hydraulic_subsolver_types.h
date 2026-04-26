#pragma once

/// Hydraulic domain types for nodal pressure-flow analysis.
///
/// Mirrors the electrical subsolver types (subsolver_types.h) with
/// domain-appropriate naming:
///   Voltage  → Pressure (kPa)
///   Current  → Flow     (L/s)
///   Conductance → Hydraulic conductance (L/(s·kPa))
///
/// Element kinds:
///   FixedPressureNode — boundary condition (e.g., atmospheric vent P=0)
///   PressureSource    — Thevenin equivalent: P_th + R_internal (e.g., fuel tank)
///   FlowBranch        — Conductance between two nodes (e.g., valve, orifice)

#include <cstdint>
#include <vector>

// == HydraulicElementKind ==
enum class HydraulicElementKind {
    FixedPressureNode,
    PressureSource,
    FlowBranch
};

// == HydraulicElement ==
struct HydraulicElement {
    HydraulicElementKind kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;        ///< P_th (PressureSource) or g_h (FlowBranch)
    float value_b;        ///< R_internal (PressureSource only); unused otherwise
    uint32_t element_id;  ///< Index into branch_flows for post-solve flow readout
};

// == HydraulicIslandPlan ==
struct HydraulicIslandPlan {
    std::vector<uint32_t> signal_indices;
    std::vector<HydraulicElement> elements;
};

// == HydraulicBuildPlan ==
struct HydraulicBuildPlan {
    std::vector<HydraulicIslandPlan> islands;
};

// == HydraulicPrimitiveHandle ==
// Identifies a specific hydraulic primitive element within an island.
// Used by wrapper components (FuelTank, SolenoidValve) to locate their
// corresponding primitive for solver integration.
struct HydraulicPrimitiveHandle {
    uint32_t island_index = UINT32_MAX;
    uint32_t element_index = UINT32_MAX;
    uint32_t element_id = UINT32_MAX;  // for indexing into branch_flows
};

/// Check if a handle points to a valid element.
inline bool is_valid(const HydraulicPrimitiveHandle& h) {
    return h.island_index != UINT32_MAX && h.element_index != UINT32_MAX;
}

// == HydraulicRuntimeState ==
// Scratch buffers for hydraulic solve. Sized to max island node count.
// All vectors retain capacity across frames — resize() keeps existing memory.
// Use reserve() at init time to pre-allocate to max island size.
struct HydraulicRuntimeState {
    /// Enable to collect per-island KCL-equivalent residual diagnostics each frame.
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
        float worst_node_pressure = 0.0f;
        float max_abs_kcl_residual = 0.0f;
        uint32_t worst_branch_element_id = UINT32_MAX;
    };

    std::vector<float> branch_flows;       ///< Solved flow through each element
    std::vector<float> element_value_a;    ///< Dynamic source values (patched each frame)
    std::vector<float> scratch_matrix;
    std::vector<float> scratch_rhs;

    std::vector<uint32_t> island_nodes;
    std::vector<std::pair<uint32_t, float>> fixed_nodes;
    std::vector<float> fixed_pressures;
    std::vector<uint8_t> is_fixed;         ///< uint8_t, NOT bool — avoids bit-packed vector
    std::vector<int> node_to_unknown;
    std::vector<float> island_pressures;
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
        fixed_pressures.reserve(max_nodes);
        is_fixed.reserve(max_nodes);
        node_to_unknown.reserve(max_nodes);
        island_pressures.reserve(max_nodes);
        kcl_residuals.reserve(max_nodes);
        scratch_matrix.reserve(static_cast<size_t>(max_unknowns) * max_unknowns);
        scratch_rhs.reserve(max_unknowns);
    }
};

/// Get the solved branch flow for a given hydraulic primitive handle.
/// Returns 0.0f for invalid handles or out-of-range indices.
/// Safe to call from component runtime code.
inline float get_branch_flow(const HydraulicRuntimeState& rt, const HydraulicPrimitiveHandle& handle) {
    if (!is_valid(handle)) {
        return 0.0f;
    }
    if (handle.element_id >= rt.branch_flows.size()) {
        return 0.0f;
    }
    return rt.branch_flows[handle.element_id];
}
