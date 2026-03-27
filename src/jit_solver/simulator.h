#pragma once

#include "jit_solver.h"
#include "state.h"
#include "SOR_constants.h"
#include <optional>
#include <string>
#include <unordered_map>

/// Empty tag type for JIT solver specialization
struct JIT_Solver {};

/// Simulator - manages component lifecycle and simulation state
/// Template parameter allows future AOT specialization: Simulator<AOT_Solver>
template<typename SolverTag>
class Simulator {
public:
    Simulator() = default;
    ~Simulator() { stop(); }

    // Non-copyable (owns simulation resources)
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&& other) noexcept;
    Simulator& operator=(Simulator&& other) noexcept;

    /// Start simulation - builds components from simulator JSON
    void start_from_json(const std::string& json_str);

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

    /// Get signal count (for testing)
    size_t get_signal_count() const { return state_.across.size(); }

    /// Get max convergence error across all dynamic signals (for diagnostics/UI)
    float get_max_convergence_error() const { return state_.get_max_change(); }

    /// Get current adaptive omega (for diagnostics/tests)
    float get_omega() const { return omega_; }

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
    /// Build result from JIT solver (owns ComponentVariant device instances)
    std::optional<BuildResult> build_result_;

    /// Simulation state (voltages, currents)
    SimulationState state_;

    /// Is simulation running?
    bool running_ = false;

    /// Accumulated simulation time
    float time_ = 0.0f;

    /// Integer step counter
    uint64_t step_count_ = 0;

    /// SOR over-relaxation factor
    float omega_ = SOR::OMEGA;

    /// Adaptive omega diagnostics/control state
    float prev_convergence_error_ = 0.0f;
    bool adaptive_omega_enabled_ = ::SORAdaptive::ENABLED;

    /// Time accumulators for sub-rate domains (FPS-independent physics)
    float accumulator_mechanical_ = 0.0f;
    float accumulator_hydraulic_ = 0.0f;
    float accumulator_thermal_ = 0.0f;
};

using JIT_Simulator = Simulator<JIT_Solver>;
