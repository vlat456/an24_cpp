#include "simulator.h"
#include "../editor/persist.h"
#include "../json_parser/json_parser.h"
#include <algorithm>

namespace an24 {

template<typename SolverTag>
Simulator<SolverTag>::Simulator(Simulator&& other) noexcept
    : build_result_(std::move(other.build_result_))
    , state_(std::move(other.state_))
    , cached_blueprint_(std::move(other.cached_blueprint_))
    , running_(other.running_)
    , time_(other.time_)
    , step_count_(other.step_count_)
    , omega_(other.omega_)
{
    other.running_ = false;
    other.time_ = 0.0f;
    other.step_count_ = 0;
}

template<typename SolverTag>
Simulator<SolverTag>& Simulator<SolverTag>::operator=(Simulator&& other) noexcept {
    if (this != &other) {
        stop();  // Clean up current resources

        build_result_ = std::move(other.build_result_);
        state_ = std::move(other.state_);
        cached_blueprint_ = std::move(other.cached_blueprint_);
        running_ = other.running_;
        time_ = other.time_;
        step_count_ = other.step_count_;
        omega_ = other.omega_;

        other.running_ = false;
        other.time_ = 0.0f;
        other.step_count_ = 0;
    }
    return *this;
}

template<typename SolverTag>
void Simulator<SolverTag>::start(const Blueprint& bp) {
    // Convert blueprint to JSON, then parse as simulator format
    std::string json_str = blueprint_to_json(bp);
    auto ctx = parse_json(json_str);

    // Build systems from parsed context
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    // Build components (this creates new Component instances)
    build_result_ = build_systems_dev(ctx.devices, connections);

    // Reset state before allocating
    state_ = SimulationState();

    // Allocate signals
    for (uint32_t i = 0; i < build_result_->signal_count; ++i) {
        bool is_fixed = std::binary_search(
            build_result_->fixed_signals.begin(),
            build_result_->fixed_signals.end(), i);
        (void)state_.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize fixed signals from RefNodes
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            auto it_sig = build_result_->port_to_signal.find(dev.name + ".v");
            if (it_sig != build_result_->port_to_signal.end()) {
                state_.across[it_sig->second] = value;
            }
        }
    }

    // Cache blueprint for potential rebuilds
    cached_blueprint_ = bp;

    // Reset time and step count
    time_ = 0.0f;
    step_count_ = 0;

    // Mark as running
    running_ = true;
}

template<typename SolverTag>
void Simulator<SolverTag>::stop() {
    // Destroy components by clearing build_result
    // Systems destructor will delete all Component pointers
    build_result_.reset();

    // Clear state
    state_ = SimulationState();

    // Reset time
    time_ = 0.0f;
    step_count_ = 0;

    // Mark as not running
    running_ = false;
}

template<typename SolverTag>
void Simulator<SolverTag>::step(float dt) {
    if (!running_ || !build_result_.has_value()) return;

    state_.clear_through();
    
    // AOT-only: component simulation is codegen'd, not runtime managed
    // JIT would call: build_result_->systems.solve_step(state_, step_count_, dt);
    
    state_.precompute_inv_conductance();

    // SOR update
    for (size_t i = 0; i < state_.across.size(); ++i) {
        if (!state_.signal_types[i].is_fixed && state_.inv_conductance[i] > 0.0f) {
            state_.across[i] += state_.through[i] * state_.inv_conductance[i] * omega_;
        }
    }

    // AOT-only: post_step is codegen'd
    // JIT would call: build_result_->systems.post_step(state_, dt);
    
    time_ += dt;
    step_count_++;
}

template<typename SolverTag>
float Simulator<SolverTag>::get_wire_voltage(const std::string& port_name) const {
    if (!build_result_.has_value()) return 0.0f;

    auto it = build_result_->port_to_signal.find(port_name);
    if (it == build_result_->port_to_signal.end()) return 0.0f;

    if (it->second >= state_.across.size()) return 0.0f;
    return state_.across[it->second];
}

template<typename SolverTag>
float Simulator<SolverTag>::get_port_value(const std::string& node_id, const std::string& port_name) const {
    return get_wire_voltage(node_id + "." + port_name);
}

template<typename SolverTag>
bool Simulator<SolverTag>::wire_is_energized(const std::string& port_name, float threshold) const {
    float v = get_wire_voltage(port_name);
    return std::abs(v) > threshold;
}

template<typename SolverTag>
void Simulator<SolverTag>::apply_overrides(const std::unordered_map<std::string, float>& overrides) {
    if (!build_result_.has_value()) return;

    for (const auto& [port_ref, value] : overrides) {
        auto it = build_result_->port_to_signal.find(port_ref);
        if (it != build_result_->port_to_signal.end()) {
            uint32_t signal_idx = it->second;
            if (signal_idx < state_.across.size()) {
                state_.across[signal_idx] = value;
            }
        }
    }
}

template<typename SolverTag>
bool Simulator<SolverTag>::get_boolean_output(const std::string& port_name) const {
    // Read from voltage signal, treat > 0.5V as true
    float value = get_wire_voltage(port_name);
    return value > 0.5f;
}

template<typename SolverTag>
bool Simulator<SolverTag>::get_component_state_as_bool(const std::string& node_id, const std::string& port_name) const {
    std::string port_key = node_id + "." + port_name;
    return get_boolean_output(port_key);
}

// Explicit template instantiation for JIT_Solver
template class Simulator<JIT_Solver>;

} // namespace an24
