#pragma once

#include <string>
#include <memory>

namespace an24 {

/// Forward declaration
class SimulationState;

/// Physical domain type
enum class Domain {
    Electrical,  // 60 Hz
    Mechanical,  // 20 Hz
    Hydraulic,   // 5 Hz
    Thermal     // 1 Hz
};

/// Component interface - base class for all devices
class Component {
public:
    virtual ~Component() = default;

    /// Component type name (for debugging)
    [[nodiscard]] virtual std::string_view type_name() const = 0;

    /// Solve electrical domain (60 Hz)
    virtual void solve_electrical(SimulationState& state) {}

    /// Solve hydraulic domain (5 Hz)
    virtual void solve_hydraulic(SimulationState& state) {}

    /// Solve mechanical domain (20 Hz)
    virtual void solve_mechanical(SimulationState& state) {}

    /// Solve thermal domain (1 Hz)
    virtual void solve_thermal(SimulationState& state) {}

    /// Post-step update (once per frame, after SOR iteration)
    virtual void post_step(SimulationState& state, float dt) {}

    /// Pre-load initialization
    virtual void pre_load() {}
};

} // namespace an24
