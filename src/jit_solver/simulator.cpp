#include "simulator.h"
#include "components/battery.h"
#include "components/controlled_voltage_source.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"
#include "../json_parser/json_parser.h"
#include "../parse_number.h"
#include <algorithm>
#include <cmath>

namespace {

/// Pre-solve pass: update dynamic Thevenin source voltages before solve_electrical().
/// ControlledVoltageSource reads cmd from the signal array (previous frame value —
/// one-frame-delay semantic) and patches its electrical plan element's value_a.
/// This must run BEFORE solve_electrical() each frame.
void update_dynamic_sources(BuildResult& br, SimulationState& st) {
    for (auto& [_name, variant] : br.devices) {
        (void)_name;
        std::visit([&](auto& comp) {
            using CompType = std::decay_t<decltype(comp)>;
            if constexpr (std::is_same_v<CompType, ControlledVoltageSource<JitProvider>>) {
                if (!is_valid(comp.electrical_handle)) {
                    return;
                }
                // Read cmd from previous frame's signal array (one-frame delay)
                float cmd = st.values[comp.provider.get(PortNames::cmd)];
                float v_source = std::clamp(cmd * comp.gain + comp.offset, comp.min_v, comp.max_v);

                // Patch the Thevenin voltage in the electrical plan
                auto& island = br.electrical_plan.islands[comp.electrical_handle.island_index];
                auto& elem = island.elements[comp.electrical_handle.element_index];
                elem.value_a = v_source;
            }
        }, variant);
    }
}

/// Commit pass for solver-owned components that need per-frame state integration.
/// These components are NOT scheduled in the push scheduler for electrical
/// propagation - they are solved via the conductance matrix. However, some have
/// commit hooks for state (e.g., Battery discharge).
/// This pass ensures their commit() is called each frame.
///
/// NOTE: Only Battery currently has non-trivial commit. All others have
/// no-op commits and could be skipped, but we call them uniformly for safety.
void commit_solver_owned_devices(BuildResult& br, SimulationState& st, float dt) {
    for (auto& [_name, variant] : br.devices) {
        (void)_name;
        std::visit([&st, dt](auto& comp) {
            using CompType = std::decay_t<decltype(comp)>;
            // Battery has commit that discharges based on solved branch current.
            // Generator, Resistor, CVS, and primitives have trivial no-op commits.
            if constexpr (std::is_same_v<CompType, Battery<JitProvider>> ||
                          std::is_same_v<CompType, Generator<JitProvider>> ||
                          std::is_same_v<CompType, Resistor<JitProvider>> ||
                          std::is_same_v<CompType, ElectricalConductance<JitProvider>> ||
                          std::is_same_v<CompType, ElectricalSource<JitProvider>> ||
                          std::is_same_v<CompType, ControlledVoltageSource<JitProvider>>) {
                comp.commit(st, dt);
            }
        }, variant);
    }
}

} // anonymous namespace

template <typename SolverTag>
Simulator<SolverTag>::Simulator(Simulator&& other) noexcept
    : build_result_(std::move(other.build_result_))
    , state_(std::move(other.state_))
    , electrical_rt_(std::move(other.electrical_rt_))
    , running_(other.running_)
    , time_(other.time_)
    , step_count_(other.step_count_) {
    other.running_ = false;
    other.time_ = 0.0f;
    other.step_count_ = 0;
}

template <typename SolverTag>
Simulator<SolverTag>& Simulator<SolverTag>::operator=(Simulator&& other) noexcept {
    if (this != &other) {
        stop();
        build_result_ = std::move(other.build_result_);
        state_ = std::move(other.state_);
        electrical_rt_ = std::move(other.electrical_rt_);
        running_ = other.running_;
        time_ = other.time_;
        step_count_ = other.step_count_;

        other.running_ = false;
        other.time_ = 0.0f;
        other.step_count_ = 0;
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
        if (dev.classname == "RefNode") {
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
        else if (dev.classname == "Value") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = locale_safe::parse_float_or(it_val->second, 0.0f);
            }

            auto it_sig = build_result_->port_to_signal.find(dev.name + ".o");
            if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
                state_.values[it_sig->second] = value;
            }
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

    // Explicitly clear electrical_rt pointer: solver-owned electrical propagation
    // runs inside step(), not between steps. Pointer must not be stale.
    state_.electrical_rt = nullptr;

    time_ = 0.0f;
    step_count_ = 0;
    running_ = true;
}

template <typename SolverTag>
void Simulator<SolverTag>::stop() {
    build_result_.reset();
    state_ = SimulationState();
    time_ = 0.0f;
    step_count_ = 0;
    running_ = false;
    // Clear pointer on stop: electrical_rt is owned by this class and must not
    // be accessed after stop. State was reset above which also zeroes the pointer.
}

template <typename SolverTag>
void Simulator<SolverTag>::step(float dt) {
    if (!running_ || !build_result_.has_value()) {
        return;
    }
    if (dt <= 0.0f) {
        return;
    }

    // Set pointer before solver runs so components can access electrical_rt.
    // RAII guard ensures cleanup on ALL exit paths (including exceptions).
    state_.electrical_rt = &electrical_rt_;
    struct RtGuard {
        SimulationState& st;
        ~RtGuard() { st.electrical_rt = nullptr; }
    } guard{state_};

    // Pre-solve: update dynamic Thevenin source voltages (ControlledVoltageSource).
    // Reads cmd from previous frame's signal array (one-frame-delay semantic).
    update_dynamic_sources(*build_result_, state_);

    // Run electrical subsolver to compute node voltages and branch currents
    solve_electrical(build_result_->electrical_plan, state_, electrical_rt_, dt);

    build_result_->scheduler.step(state_, dt);

    // Explicit commit pass for solver-owned components (Battery discharge, etc).
    // These are NOT in the scheduler's source/consumer lists but need per-frame
    // commit to update internal state (e.g., Battery.charge).
    commit_solver_owned_devices(*build_result_, state_, dt);

    // Guard destructor clears state_.electrical_rt = nullptr here.

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
        return 0.0f;
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

template <typename SolverTag>
double Simulator<SolverTag>::get_battery_charge(const std::string& device_name) const {
    if (!running_ || !build_result_.has_value()) {
        return 0.0;
    }

    auto it = build_result_->devices.find(device_name);
    if (it == build_result_->devices.end()) {
        return 0.0;
    }

    const Battery<JitProvider>* batt = std::get_if<Battery<JitProvider>>(&it->second);
    if (batt == nullptr) {
        return 0.0;
    }

    return batt->charge;
}

template class Simulator<JIT_Solver>;
