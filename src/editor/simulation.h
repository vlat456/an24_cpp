#pragma once

#include "data/blueprint.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include <optional>
#include <string>

using namespace an24;

/// SimulationController - wraps JIT solver for real-time editor simulation
struct SimulationController {
    /// Build result from JIT solver
    std::optional<BuildResult> build_result;

    /// Simulation state (voltages, currents)
    SimulationState state;

    /// Is simulation running?
    bool running = false;

    /// Accumulated simulation time
    float time = 0.0f;

    /// SOR over-relaxation factor
    float omega = 1.8f;

    /// Time step (60 Hz)
    float dt = 0.016f;

    /// Build simulation from blueprint
    void build(const Blueprint& bp);

    /// Run one simulation step
    void step(float dt);

    /// Start simulation
    void start() { running = true; }

    /// Stop simulation
    void stop() { running = false; }

    /// Get voltage at a port (e.g., "battery.v_out")
    float get_wire_voltage(const std::string& port_name) const;

    /// Check if wire has voltage difference (for highlighting)
    bool wire_has_current(const std::string& wire_id) const;
};
