#pragma once

#include "jit_solver.h"
#include "state.h"
#include "subsolvers/electrical_subsolver.h"
#include <optional>
#include <string>
#include <unordered_map>

/// Empty tag type for JIT solver specialization.
struct JIT_Solver {};

template <typename SolverTag>
class Simulator {
public:
    /// E-008: Maximum dt per step to prevent physics explosions.
    /// Frame hitches beyond this are clamped, not accumulated.
    static constexpr double MAX_DT = 0.1;

    Simulator() = default;
    ~Simulator() { stop(); }

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&& other) noexcept;
    Simulator& operator=(Simulator&& other) noexcept;

    void start_from_json(const std::string& json_str);
    void stop();
    void step(double dt);

    bool is_running() const { return running_; }
    bool is_built() const { return build_result_.has_value(); }

    double get_time() const { return time_; }
    uint64_t get_step_count() const { return step_count_; }

    size_t get_signal_count() const { return state_.values.size(); }

    float get_wire_voltage(const std::string& port_name) const;
    float get_port_value(const std::string& node_id, const std::string& port_name) const;
    bool wire_is_energized(const std::string& port_name, float threshold = 0.5f) const;

    void apply_overrides(const std::unordered_map<std::string, float>& overrides);

    bool get_boolean_output(const std::string& port_name) const;
    bool get_component_state_as_bool(const std::string& node_id, const std::string& port_name) const;

    /// Get battery charge for a named device (test visibility).
    /// Returns 0.0 if simulator not running, device missing, or not a Battery.
    /// Uses double because charge is a running accumulator where float32 precision
    /// is insufficient (per-step delta can be below float32 ULP at large charge values).
    double get_battery_charge(const std::string& device_name) const;

private:
    std::optional<BuildResult> build_result_;
    SimulationState state_;
    ElectricalRuntimeState electrical_rt_;

    bool running_ = false;
    double time_ = 0.0;
    uint64_t step_count_ = 0;
};

using JIT_Simulator = Simulator<JIT_Solver>;
