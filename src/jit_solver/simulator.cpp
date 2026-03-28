#include "simulator.h"
#include "../json_parser/json_parser.h"
#include "../parse_number.h"
#include <algorithm>
#include <cmath>

template <typename SolverTag>
Simulator<SolverTag>::Simulator(Simulator&& other) noexcept
    : build_result_(std::move(other.build_result_))
    , state_(std::move(other.state_))
    , running_(other.running_)
    , time_(other.time_)
    , step_count_(other.step_count_)
    , accumulator_mechanical_(other.accumulator_mechanical_)
    , accumulator_hydraulic_(other.accumulator_hydraulic_)
    , accumulator_thermal_(other.accumulator_thermal_) {
    other.running_ = false;
    other.time_ = 0.0f;
    other.step_count_ = 0;
    other.accumulator_mechanical_ = 0.0f;
    other.accumulator_hydraulic_ = 0.0f;
    other.accumulator_thermal_ = 0.0f;
}

template <typename SolverTag>
Simulator<SolverTag>& Simulator<SolverTag>::operator=(Simulator&& other) noexcept {
    if (this != &other) {
        stop();
        build_result_ = std::move(other.build_result_);
        state_ = std::move(other.state_);
        running_ = other.running_;
        time_ = other.time_;
        step_count_ = other.step_count_;
        accumulator_mechanical_ = other.accumulator_mechanical_;
        accumulator_hydraulic_ = other.accumulator_hydraulic_;
        accumulator_thermal_ = other.accumulator_thermal_;

        other.running_ = false;
        other.time_ = 0.0f;
        other.step_count_ = 0;
        other.accumulator_mechanical_ = 0.0f;
        other.accumulator_hydraulic_ = 0.0f;
        other.accumulator_thermal_ = 0.0f;
    }
    return *this;
}

template <typename SolverTag>
void Simulator<SolverTag>::start_from_json(const std::string& json_str) {
    auto ctx = parse_json(json_str);

    std::vector<std::pair<std::string, std::string>> connections;
    connections.reserve(ctx.connections.size());
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    build_result_ = build_systems_dev(ctx.devices, connections);

    state_ = SimulationState();
    // Phase 1: allocate all signals as fixed (append-only) so that
    // logical signal IDs from port_to_signal match physical indices.
    // Dynamic/fixed partitioning is a Phase 2 concern when components
    // actually need the split layout.
    for (uint32_t i = 0; i < build_result_->signal_count; ++i) {
        (void)state_.allocate_signal(0.0f, {Domain::Electrical, /*is_fixed=*/true});
    }

    for (const auto& dev : ctx.devices) {
        if (dev.classname != "RefNode") {
            continue;
        }

        float value = 0.0f;
        auto it_val = dev.params.find("value");
        if (it_val != dev.params.end()) {
            value = locale_safe::parse_float_or(it_val->second, 0.0f);
        }

        auto it_sig = build_result_->port_to_signal.find(dev.name + ".v");
        if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
            state_.values[it_sig->second] = value;
        }
    }

    // Apply explicit initial values from JSON after baseline signal initialization.
    // Keys are port references like "device.port".
    for (const auto& [port_ref, value] : ctx.initial_values) {
        auto it_sig = build_result_->port_to_signal.find(port_ref);
        if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
            state_.values[it_sig->second] = value;
        }
    }

    state_.lut_keys = std::move(build_result_->lut_keys);
    state_.lut_values = std::move(build_result_->lut_values);

    time_ = 0.0f;
    step_count_ = 0;
    accumulator_mechanical_ = 0.0f;
    accumulator_hydraulic_ = 0.0f;
    accumulator_thermal_ = 0.0f;
    running_ = true;
}

template <typename SolverTag>
void Simulator<SolverTag>::stop() {
    build_result_.reset();
    state_ = SimulationState();
    time_ = 0.0f;
    step_count_ = 0;
    accumulator_mechanical_ = 0.0f;
    accumulator_hydraulic_ = 0.0f;
    accumulator_thermal_ = 0.0f;
    running_ = false;
}

template <typename SolverTag>
void Simulator<SolverTag>::step(float dt) {
    if (!running_ || !build_result_.has_value()) {
        return;
    }
    if (dt <= 0.0f) {
        return;
    }

    build_result_->scheduler.step(state_, dt);

    accumulator_mechanical_ += dt;
    accumulator_hydraulic_ += dt;
    accumulator_thermal_ += dt;

    time_ += dt;
    step_count_++;
}

template <typename SolverTag>
float Simulator<SolverTag>::get_wire_voltage(const std::string& port_name) const {
    if (!build_result_.has_value()) {
        return 0.0f;
    }

    auto it = build_result_->port_to_signal.find(port_name);
    if (it == build_result_->port_to_signal.end()) {
        auto dot = port_name.find('.');
        if (dot == std::string::npos) {
            return 0.0f;
        }

        std::string fallback = port_name.substr(0, dot) + ":" + port_name.substr(dot + 1) + ".ext";
        it = build_result_->port_to_signal.find(fallback);
        if (it == build_result_->port_to_signal.end()) {
            return 0.0f;
        }
    }

    if (it->second >= state_.values.size()) {
        return 0.0f;
    }
    return state_.values[it->second];
}

template <typename SolverTag>
float Simulator<SolverTag>::get_port_value(const std::string& node_id, const std::string& port_name) const {
    return get_wire_voltage(node_id + "." + port_name);
}

template <typename SolverTag>
bool Simulator<SolverTag>::wire_is_energized(const std::string& port_name, float threshold) const {
    return std::abs(get_wire_voltage(port_name)) > threshold;
}

template <typename SolverTag>
void Simulator<SolverTag>::apply_overrides(const std::unordered_map<std::string, float>& overrides) {
    if (!build_result_.has_value()) {
        return;
    }

    for (const auto& [port_ref, value] : overrides) {
        auto it = build_result_->port_to_signal.find(port_ref);
        if (it != build_result_->port_to_signal.end() && it->second < state_.values.size()) {
            state_.values[it->second] = value;
        }
    }
}

template <typename SolverTag>
bool Simulator<SolverTag>::get_boolean_output(const std::string& port_name) const {
    return get_wire_voltage(port_name) > 0.5f;
}

template <typename SolverTag>
bool Simulator<SolverTag>::get_component_state_as_bool(const std::string& node_id, const std::string& port_name) const {
    return get_boolean_output(node_id + "." + port_name);
}

template class Simulator<JIT_Solver>;
