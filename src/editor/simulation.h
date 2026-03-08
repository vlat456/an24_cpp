#pragma once

#include "data/blueprint.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include <optional>
#include <string>

// BUGFIX [a4f9c3] Removed file-scope 'using namespace an24' — pollutes all includers

/// SimulationController - wraps JIT solver for real-time editor simulation
struct SimulationController {
    /// Build result from JIT solver
    std::optional<an24::BuildResult> build_result;

    /// Simulation state (voltages, currents)
    an24::SimulationState state;

    /// Is simulation running?
    bool running = false;

    /// Accumulated simulation time
    float time = 0.0f;

    /// Integer step counter (avoids float drift)
    uint64_t step_count = 0;

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

    /// Reset simulation to initial state
    void reset();

    /// Get voltage at a port (e.g., "battery.v_out")
    float get_wire_voltage(const std::string& port_name) const;

    /// Get value at a port by node_id and port_name
    float get_port_value(const std::string& node_id, const std::string& port_name) const;

    /// Check if a wire is energized (its signal voltage is nonzero)
    bool wire_is_energized(const std::string& port_name, float threshold = 0.5f) const;

    /// Apply signal overrides (for button clicks, etc.)
    /// Maps "node_id.port_name" -> voltage value
    void apply_overrides(const std::unordered_map<std::string, float>& overrides);
};
