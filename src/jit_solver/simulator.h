#pragma once

#include "jit_solver.h"
#include "state.h"
#include "systems.h"
#include "SOR_constants.h"
#include "../editor/data/blueprint.h"
#include <optional>
#include <unordered_map>

namespace an24 {

/// Empty tag type for JIT solver specialization
struct JIT_Solver {};

/// Simulator - manages component lifecycle and simulation state
/// Template parameter allows future AOT specialization: Simulator<AOT_Solver>
template<typename SolverTag>
class Simulator {
public:
    Simulator() = default;
    ~Simulator() { stop(); }

    // Non-copyable (owns unique Component pointers via Systems)
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&& other) noexcept;
    Simulator& operator=(Simulator&& other) noexcept;

    /// Start simulation - builds components from blueprint
    void start(const Blueprint& bp);

    /// Stop simulation - destroys components (clears component state!)
    void stop();

    /// Run one simulation step
    void step(float dt);

    /// Check if simulation is running
    bool is_running() const { return running_; }

    /// Check if components are built
    bool is_built() const { return build_result_.has_value(); }

    /// Get current simulation time
    float get_time() const { return time_; }

    /// Get step count
    uint64_t get_step_count() const { return step_count_; }

    /// Get voltage at a port (e.g., "battery.v_out")
    float get_wire_voltage(const std::string& port_name) const;

    /// Get value at a port by node_id and port_name
    float get_port_value(const std::string& node_id, const std::string& port_name) const;

    /// Check if a wire is energized
    bool wire_is_energized(const std::string& port_name, float threshold = 0.5f) const;

    /// Apply signal overrides (for button clicks, etc.)
    void apply_overrides(const std::unordered_map<std::string, float>& overrides);

    /// Get boolean output from a component (e.g., "comparator.o")
    bool get_boolean_output(const std::string& port_name) const;

    /// Get boolean output by node_id and port_name
    bool get_component_state_as_bool(const std::string& node_id, const std::string& port_name) const;

private:
    /// Build result from JIT solver (owns Component pointers!)
    std::optional<BuildResult> build_result_;

    /// Simulation state (voltages, currents)
    SimulationState state_;

    /// Cached blueprint for rebuilds
    Blueprint cached_blueprint_;

    /// Is simulation running?
    bool running_ = false;

    /// Accumulated simulation time
    float time_ = 0.0f;

    /// Integer step counter
    uint64_t step_count_ = 0;

    /// SOR over-relaxation factor
    float omega_ = SOR::OMEGA;

    /// Time accumulators for sub-rate domains (FPS-independent physics)
    float accumulator_mechanical_ = 0.0f;
    float accumulator_hydraulic_ = 0.0f;
    float accumulator_thermal_ = 0.0f;
};

// Type alias for backward compatibility
using JIT_Simulator = Simulator<JIT_Solver>;

} // namespace an24
