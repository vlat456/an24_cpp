#pragma once

#include "component.h"
#include <array>
#include <vector>
#include <memory>

namespace an24 {

/// Systems container - groups components by domain
class Systems {
public:
    /// Electrical components (60 Hz)
    std::vector<std::unique_ptr<Component>> electrical;

    /// Hydraulic components (5 Hz) - 12 buckets
    std::array<std::vector<std::unique_ptr<Component>>, 12> hydraulic;

    /// Mechanical components (20 Hz) - 3 buckets
    std::array<std::vector<std::unique_ptr<Component>>, 3> mechanical;

    /// Thermal components (1 Hz) - 60 buckets
    std::array<std::vector<std::unique_ptr<Component>>, 60> thermal;

    /// Add electrical component
    void add_electrical(std::unique_ptr<Component> comp);

    /// Add hydraulic component to bucket
    void add_hydraulic(size_t bucket, std::unique_ptr<Component> comp);

    /// Add mechanical component to bucket
    void add_mechanical(size_t bucket, std::unique_ptr<Component> comp);

    /// Add thermal component to bucket
    void add_thermal(size_t bucket, std::unique_ptr<Component> comp);

    /// Total component count
    [[nodiscard]] size_t component_count() const;

    /// Run one solver step
    void solve_step(SimulationState& state, size_t step);

    /// Post-step updates
    void post_step(SimulationState& state, float dt);

    /// Pre-load initialization
    void pre_load();
};

} // namespace an24
