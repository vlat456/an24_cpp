#include "simulator.h"
#include "components/battery.h"
#include "components/controlled_voltage_source.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"
#include "components/azs.h"
#include "components/hold_button.h"
#include "components/relay.h"
#include "components/variable_conductance.h"
#include "components/port_registry.h"
#include "../json_parser/json_parser.h"
#include "../parse_number.h"
#include <algorithm>
#include <cmath>

namespace {

/// Pre-solve pass: update dynamic Thevenin source voltages before solve_electrical().
/// Uses pre-built typed pointer lists (SolverOwnedRefs) to avoid per-frame
/// std::visit scan over all 68+ ComponentVariant types.
void update_dynamic_sources(BuildResult& br, SimulationState& st) {
    for (auto* comp : br.solver_owned.controlled_voltage_sources) {
        if (!is_valid(comp->electrical_handle)) continue;
        float cmd = st.values[comp->provider.get(PortNames::cmd)];
        float gain = st.values[comp->provider.get(PortNames::gain)];
        float offset = st.values[comp->provider.get(PortNames::offset)];
        float min_v = st.values[comp->provider.get(PortNames::min_v)];
        float max_v = st.values[comp->provider.get(PortNames::max_v)];
        float v_source = std::clamp(cmd * gain + offset, min_v, max_v);
        auto& island = br.electrical_plan.islands[comp->electrical_handle.island_index];
        auto& elem = island.elements[comp->electrical_handle.element_index];
        elem.value_a = v_source;
    }

    for (auto* comp : br.solver_owned.variable_conductances) {
        if (!is_valid(comp->electrical_handle)) continue;
        float cmd = st.values[comp->provider.get(PortNames::cmd)];
        float g_min = st.values[comp->provider.get(PortNames::g_min)];
        float g_max = st.values[comp->provider.get(PortNames::g_max)];
        float t = std::clamp(cmd, 0.0f, 1.0f);
        float g = g_min + (g_max - g_min) * t;
        auto& island = br.electrical_plan.islands[comp->electrical_handle.island_index];
        auto& elem = island.elements[comp->electrical_handle.element_index];
        elem.value_a = g;
    }

    for (auto* comp : br.solver_owned.azs_switches) {
        if (!is_valid(comp->electrical_handle)) continue;
        float g = comp->closed ? comp->g_closed : comp->g_open;
        auto& island = br.electrical_plan.islands[comp->electrical_handle.island_index];
        auto& elem = island.elements[comp->electrical_handle.element_index];
        elem.value_a = g;
    }

    for (auto* comp : br.solver_owned.hold_buttons) {
        if (!is_valid(comp->electrical_handle)) continue;
        float g = comp->is_pressed ? comp->g_closed : comp->g_open;
        auto& island = br.electrical_plan.islands[comp->electrical_handle.island_index];
        auto& elem = island.elements[comp->electrical_handle.element_index];
        elem.value_a = g;
    }

    for (auto* comp : br.solver_owned.relays) {
        if (!is_valid(comp->electrical_handle)) continue;
        float g = comp->closed ? comp->g_closed : comp->g_open;
        auto& island = br.electrical_plan.islands[comp->electrical_handle.island_index];
        auto& elem = island.elements[comp->electrical_handle.element_index];
        elem.value_a = g;
    }
}

/// Commit pass for solver-owned components that need per-frame state integration.
/// Uses pre-built typed pointer lists (SolverOwnedRefs) to avoid per-frame
/// std::visit scan over all 68+ ComponentVariant types.
void commit_solver_owned_devices(BuildResult& br, SimulationState& st, double dt) {
    for (auto* comp : br.solver_owned.batteries) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.generators) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.resistors) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.electrical_conductances) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.electrical_sources) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.controlled_voltage_sources) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.variable_conductances) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.azs_switches) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.hold_buttons) { comp->commit(st, dt); }
    for (auto* comp : br.solver_owned.relays) { comp->commit(st, dt); }
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
    other.time_ = 0.0;
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
        other.time_ = 0.0;
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

    time_ = 0.0;
    step_count_ = 0;
    running_ = true;
}

template <typename SolverTag>
void Simulator<SolverTag>::stop() {
    build_result_.reset();
    state_ = SimulationState();
    time_ = 0.0;
    step_count_ = 0;
    running_ = false;
    // Clear pointer on stop: electrical_rt is owned by this class and must not
    // be accessed after stop. State was reset above which also zeroes the pointer.
}

template <typename SolverTag>
void Simulator<SolverTag>::step(double dt) {
    if (!running_ || !build_result_.has_value()) {
        return;
    }
    if (dt <= 0.0) {
        return;
    }

    // E-008: Clamp dt to prevent physics explosions on frame hitches.
    // At variable refresh rates, dt can spike to very large values (alt-tab,
    // loading screen, frame stutter). Components using Euler integration
    // (value += rate * dt) would produce non-physical discontinuities:
    // a 1-second hitch at 1000ms dt could discharge a battery by 60x normal,
    // slam integrators to limits, or cause flickering gauges.
    // 100ms cap = ~6 frames at 60Hz, which is the most we allow to accumulate
    // in a single step. Standard practice in game physics.
    static constexpr double MAX_DT = 0.1;
    dt = std::min(dt, MAX_DT);

    // ===========================================================================
    // Simulation pipeline (E-009: single-solve architecture)
    // ===========================================================================
    //
    // The pipeline uses ONE electrical solve per frame with one-frame-delayed
    // actuator states. This is intentional for a game at 60Hz+:
    //
    //   1. update_dynamic_sources — stamp actuator states (AZS/Relay/CVS)
    //      from PREVIOUS frame's commit into the electrical build plan
    //   2. solve_electrical — single Gaussian solve for all islands
    //   3. scheduler.step — execute all logical/mechanical/etc. components
    //   4. commit_solver_owned_devices — battery discharge, state transitions
    //
    // The previous 9-phase pipeline ran two electrical solves per frame to
    // handle within-frame actuator feedback. At 60Hz+, one-frame delay
    // (16ms) for AZS/relay state changes is invisible to players, and
    // halving the electrical solver cost is a clear win for a game.
    //
    // One-frame delay semantics: actuator state changes (e.g., AZS toggle,
    // relay close) take effect in the NEXT frame's solve_electrical, not
    // the current one. This is consistent with the push-model design where
    // commit() stages transitions for next frame's execute().
    // ===========================================================================

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
