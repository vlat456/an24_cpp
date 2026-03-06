#pragma once

#include <string>
#include <memory>

// Include shared types from json_parser
#include "../json_parser/json_parser.h"

namespace an24 {

/// Forward declarations
class SimulationState;
class PushState;

/// Component flags for push solver execution phases (bitmask)
enum class ComponentFlags : uint32_t {
    None = 0,
    VoltageSource = 1 << 0,      // Phase 1: produces voltage (Battery, RefNode, Generator, RU19A)
    StateMachine = 1 << 1,       // Phase 2: has update_state() (Switch, HoldButton, GS24, RU19A)
    PropagatesResistance = 1 << 2 // Phase 0: computes load resistance (Resistor, Light, Wire)
};

inline constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr ComponentFlags operator&(ComponentFlags a, ComponentFlags b) {
    return static_cast<ComponentFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// Component interface - base class for all devices
class Component {
public:
    virtual ~Component() = default;

    /// Component type name (for debugging)
    [[nodiscard]] virtual std::string_view type_name() const = 0;

    /// Component flags for push solver execution (bitmask, allows hybrid components)
    [[nodiscard]] virtual ComponentFlags flags() const = 0;

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

    // ========== Push Solver Interface ==========

    /// Is this component a voltage source? (Phase 1: sources produce voltage)
    virtual bool is_voltage_source() const { return false; }

    /// Does this component have state machine logic? (Phase 2: update_state)
    virtual bool has_state_machine() const { return false; }

    /// Push voltage to outputs (Phase 4: propagate voltage)
    virtual void push_voltage(PushState& state, float dt) { (void)state; (void)dt; }

    /// Update internal state (Phase 2: state machines)
    virtual void update_state(PushState& state, float dt) { (void)state; (void)dt; }

    /// Propagate resistance upstream (Phase 0: compute load resistance)
    virtual void propagate_resistance(PushState& state) { (void)state; }
};

} // namespace an24
