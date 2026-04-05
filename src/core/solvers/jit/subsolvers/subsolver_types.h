#pragma once

#include <cstdint>
#include <vector>

// == ElectricalElementKind ==
enum class ElectricalElementKind {
    FixedVoltageNode,
    TheveninSource,
    ConductanceBranch
};

// == ElectricalElement ==
struct ElectricalElement {
    ElectricalElementKind kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    uint32_t element_id;
};

// == ElectricalIslandPlan ==
struct ElectricalIslandPlan {
    std::vector<uint32_t> signal_indices;
    std::vector<ElectricalElement> elements;
};

// == ElectricalBuildPlan ==
struct ElectricalBuildPlan {
    std::vector<ElectricalIslandPlan> islands;
};

// == ElectricalPrimitiveHandle ==
// Identifies a specific electrical primitive element within an island.
// Used by wrapper components (Battery, Generator, IndicatorLight, CurrentSense)
// to locate their corresponding primitive for solver integration.
struct ElectricalPrimitiveHandle {
    uint32_t island_index = UINT32_MAX;
    uint32_t element_index = UINT32_MAX;
    uint32_t element_id = UINT32_MAX;  // for indexing into branch_currents
};

// Helper to check if a handle points to a valid element
inline bool is_valid(const ElectricalPrimitiveHandle& h) {
    return h.island_index != UINT32_MAX && h.element_index != UINT32_MAX;
}

// == ElectricalRuntimeState ==
// Scratch buffers for electrical solve. Sized to max island node count.
// All vectors retain capacity across frames — resize() keeps existing memory.
// Use reserve() at init time to pre-allocate to max island size.
struct ElectricalRuntimeState {
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
        float worst_node_voltage = 0.0f;
        float max_abs_kcl_residual = 0.0f;
        uint32_t worst_branch_element_id = UINT32_MAX;
    };

    std::vector<float> branch_currents;
    std::vector<float> element_value_a;
    std::vector<float> scratch_matrix;
    std::vector<float> scratch_rhs;

    std::vector<uint32_t> island_nodes;
    std::vector<std::pair<uint32_t, float>> fixed_nodes;
    std::vector<float> fixed_voltages;
    std::vector<uint8_t> is_fixed;  // uint8_t, NOT bool — std::vector<bool> is bit-packed and slow
    std::vector<int> node_to_unknown;
    std::vector<float> island_voltages;
    std::vector<float> kcl_residuals;

    std::vector<IslandDiagnostic> island_diagnostics;
    SolveCounters counters;

    void reset_counters() {
        counters = {};
    }

    void reserve(uint32_t max_nodes, uint32_t max_elements, uint32_t max_element_id) {
        uint32_t max_unknowns = max_nodes;
        branch_currents.reserve(max_element_id + 1);
        element_value_a.reserve(max_element_id + 1);
        island_nodes.reserve(max_nodes);
        fixed_nodes.reserve(max_elements);
        fixed_voltages.reserve(max_nodes);
        is_fixed.reserve(max_nodes);
        node_to_unknown.reserve(max_nodes);
        island_voltages.reserve(max_nodes);
        kcl_residuals.reserve(max_nodes);
        scratch_matrix.reserve(static_cast<size_t>(max_unknowns) * max_unknowns);
        scratch_rhs.reserve(max_unknowns);
    }
};

/// Get the solved branch current for a given electrical primitive handle.
/// Returns 0.0f for invalid handles or out-of-range indices.
/// This is safe to call from component runtime code.
inline float get_branch_current(const ElectricalRuntimeState& rt, const ElectricalPrimitiveHandle& handle) {
    if (!is_valid(handle)) {
        return 0.0f;
    }
    if (handle.element_id >= rt.branch_currents.size()) {
        return 0.0f;
    }
    return rt.branch_currents[handle.element_id];
}
