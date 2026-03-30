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
    uint32_t component_index;
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
    uint32_t component_index = UINT32_MAX;  // for indexing into branch_currents
};

// Helper to check if a handle points to a valid element
inline bool is_valid(const ElectricalPrimitiveHandle& h) {
    return h.island_index != UINT32_MAX && h.element_index != UINT32_MAX;
}

// == ElectricalRuntimeState ==
struct ElectricalRuntimeState {
    std::vector<float> branch_currents;
    std::vector<float> scratch_matrix;
    std::vector<float> scratch_rhs;
};

/// Get the solved branch current for a given electrical primitive handle.
/// Returns 0.0f for invalid handles or out-of-range indices.
/// This is safe to call from component runtime code.
inline float get_branch_current(const ElectricalRuntimeState& rt, const ElectricalPrimitiveHandle& handle) {
    if (!is_valid(handle)) {
        return 0.0f;
    }
    if (handle.component_index >= rt.branch_currents.size()) {
        return 0.0f;
    }
    return rt.branch_currents[handle.component_index];
}
