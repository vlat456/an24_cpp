#pragma once

#include "component.h"
#include <array>
#include <vector>
#include <memory>

/// Systems container - groups components by domain
class Systems {
public:
    /// All components (centralized ownership)
    std::vector<std::unique_ptr<Component>> all_components;

    /// Electrical components (60 Hz) - pointers to all_components
    std::vector<Component*> electrical;

    /// Logical components (60 Hz) - boolean logic operations
    std::vector<Component*> logical;

    /// Hydraulic components (5 Hz) - 12 buckets of pointers
    std::array<std::vector<Component*>, 12> hydraulic;

    /// Mechanical components (20 Hz) - 3 buckets of pointers
    std::array<std::vector<Component*>, 3> mechanical;

    /// Thermal components (1 Hz) - 60 buckets of pointers
    std::array<std::vector<Component*>, 60> thermal;

    /// Time accumulators for each domain (ensures FPS-independent physics)
    float accumulator_mechanical = 0.0f;   // accumulated dt for 20 Hz updates
    float accumulator_hydraulic = 0.0f;    // accumulated dt for 5 Hz updates
    float accumulator_thermal = 0.0f;      // accumulated dt for 1 Hz updates

    /// Add component and register it in all applicable domains
    void add_component(std::unique_ptr<Component> comp, const std::vector<Domain>& domains);

    /// Add electrical component (legacy, for compatibility)
    void add_electrical(std::unique_ptr<Component> comp);

    /// Add hydraulic component to bucket
    void add_hydraulic(size_t bucket, std::unique_ptr<Component> comp);

    /// Add mechanical component to bucket
    void add_mechanical(size_t bucket, std::unique_ptr<Component> comp);

    /// Add thermal component to bucket
    void add_thermal(size_t bucket, std::unique_ptr<Component> comp);

    /// Total component count
    [[nodiscard]] size_t component_count() const;

    /// Run one solver step (all domains except logical)
    void solve_step(SimulationState& state, size_t step, float dt);

    /// Solve logical domain - must be called AFTER SOR + post_step
    /// so logical gates read converged electrical values
    void solve_logical(SimulationState& state, float dt);

    /// Post-step updates
    void post_step(SimulationState& state, float dt);

    /// Pre-load initialization
    void pre_load();
};
