#include "simulator.h"
#include "build_common.h"
#include "core/solvers/common/signal_key.h"
#include "subsolvers/hydraulic_subsolver.h"
#include "../../../parse_number.h"
#include <algorithm>
#include <cmath>

namespace {

/// Pre-solve pass: apply compiled electrical patch operations.
void update_dynamic_sources(BuildResult& br, SimulationState& st, ElectricalRuntimeState& rt) {
    for (const auto& op : br.electrical.patch_ops) {
        if (op.element_id >= rt.element_value_a.size()) {
            continue;
        }

        float out = rt.element_value_a[op.element_id];
        switch (op.kind) {
            case ElectricalPatchKind::AffineClamp: {
                float cmd = st.values[op.s0];
                float gain = st.values[op.s1];
                float offset = st.values[op.s2];
                float min_v = st.values[op.s3];
                float max_v = st.values[op.s4];
                out = std::clamp(cmd * gain + offset, min_v, max_v);
                break;
            }
            case ElectricalPatchKind::LerpClamped01: {
                float cmd = st.values[op.s0];
                float lo = st.values[op.s1];
                float hi = st.values[op.s2];
                float t = std::clamp(cmd, 0.0f, 1.0f);
                out = lo + (hi - lo) * t;
                break;
            }
            case ElectricalPatchKind::BoolSwitch: {
                const bool state = st.values[op.s0] > 0.5f;
                out = state ? op.closed_value : op.open_value;
                break;
            }
            case ElectricalPatchKind::IndexSwitch: {
                const int idx = static_cast<int>(st.values[op.s0]);
                out = (idx == op.index_value) ? op.closed_value : op.open_value;
                break;
            }
        }
        rt.element_value_a[op.element_id] = out;
    }
}

/// Commit pass for solver-owned components that need per-frame state integration.
/// Uses compiled commit ops to avoid per-frame per-type branching.
void run_solver_owned_ops(const std::vector<SolverStepOp>& ops, SimulationState& st, double dt) {
    for (const auto& op : ops) {
        if (op.instance != nullptr && op.fn != nullptr) {
            op.fn(op.instance, st, dt);
        }
    }
}

} // anonymous namespace

// Hydraulic runtime helpers — same pattern as electrical.
namespace {

void update_hydraulic_dynamic_sources(BuildResult& br, SimulationState& st, HydraulicRuntimeState& rt) {
    for (const auto& op : br.hydraulic.patch_ops) {
        if (op.element_id >= rt.element_value_a.size()) {
            continue;
        }

        float out = rt.element_value_a[op.element_id];
        switch (op.kind) {
            case HydraulicPatchKind::BoolSwitch: {
                const bool state = st.values[op.s0] > 0.5f;
                out = state ? op.closed_value : op.open_value;
                break;
            }
            case HydraulicPatchKind::CopySignal: {
                out = st.values[op.s0];
                break;
            }
        }
        rt.element_value_a[op.element_id] = out;
    }
}

} // anonymous namespace

template <typename SolverTag>
Simulator<SolverTag>::Simulator(Simulator&& other) noexcept
    : build_result_(std::move(other.build_result_))
    , state_(std::move(other.state_))
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
void Simulator<SolverTag>::start(const JitBuildInput& input) {
    build_result_ = build_systems_dev(input);

    state_ = SimulationState();
    for (uint32_t i = 0; i < build_result_->signal_count; ++i) {
        (void)state_.allocate_signal(0.0f);
    }

    // Bootstrap: run solver-owned commit ops BEFORE initializing Value/RefNode
    // signals. This ensures components like AZS/Relay write their initial
    // internal state (closed=true) to signal ports. Running BEFORE Value init
    // means control signals are still 0.0, so no premature state transitions
    // occur — components simply write their constructor-initialized state.
    run_solver_owned_ops(build_result_->electrical.commit_ops, state_, 0.0);

    // Bootstrap hydraulic commit ops (same pattern as electrical).
    run_solver_owned_ops(build_result_->hydraulic.commit_ops, state_, 0.0);

    // Bootstrap hydraulic execute ops so CopySignal patch ops have valid
    // p_source values on the first frame (e.g., FuelTank gravity pressure).
    // Without this, element_value_a would use plan-default pressure (0.0)
    // instead of the computed gravity head for a full tank.
    run_solver_owned_ops(build_result_->hydraulic.execute_ops, state_, 0.0);

    // Initialize RefNode and Value devices from their params.
    for (const auto& dev : input.devices) {
        if (dev.kind == ComponentKind::RefNode) {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = locale_safe::parse_float_or(it_val->second, 0.0f);
            }
            const core::InternedId key = build_result_->signal_key_interner.lookup(
                signal_key::make_node_port_key(dev.name, "v"));
            auto it_sig = build_result_->port_to_signal.find(key);
            if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
                state_.values[it_sig->second] = value;
            }
        }
        else if (dev.kind == ComponentKind::Value) {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = locale_safe::parse_float_or(it_val->second, 0.0f);
            }
            const core::InternedId key = build_result_->signal_key_interner.lookup(
                signal_key::make_node_port_key(dev.name, "o"));
            auto it_sig = build_result_->port_to_signal.find(key);
            if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
                state_.values[it_sig->second] = value;
            }
        }
    }

    // Apply explicit initial values from JitBuildInput (if any).
    // Keys are port references like "device.port" — interned for typed lookup.
    for (const auto& [port_ref, value] : input.initial_values) {
        const core::InternedId key = build_result_->signal_key_interner.lookup(port_ref);
        auto it_sig = build_result_->port_to_signal.find(key);
        if (it_sig != build_result_->port_to_signal.end() && it_sig->second < state_.values.size()) {
            state_.values[it_sig->second] = value;
        }
    }

    state_.lut_keys = std::move(build_result_->lut_keys);
    state_.lut_values = std::move(build_result_->lut_values);

    state_.electrical_rt = nullptr;
    state_.hydraulic_rt = nullptr;

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
    // Note: electrical_rt pointer in state_ is already cleared by SimulationState()
    // reset above. build_result_.reset() destroys the ElectricalArtifacts which
    // owns the ElectricalRuntimeState. No dangling pointer possible.
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
    //   3. execute solver-owned — post-solve compute (e.g., AZS thermal model)
    //   4. scheduler.step — execute all logical/mechanical/etc. components
    //   5. commit solver-owned — battery discharge, state transitions
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
    state_.electrical_rt = &build_result_->electrical.runtime;
    struct RtGuard {
        SimulationState& st;
        ~RtGuard() { st.electrical_rt = nullptr; st.hydraulic_rt = nullptr; }
    } guard{state_};

    // Ensure runtime mutable element values are initialized from build-plan defaults.
    jit_solver_impl::build_common::init_element_values_from_plan(build_result_->electrical.plan, build_result_->electrical.runtime);

    // Pre-solve: update dynamic Thevenin source voltages (ControlledVoltageSource).
    // Reads cmd from previous frame's signal array (one-frame-delay semantic).
    update_dynamic_sources(*build_result_, state_, build_result_->electrical.runtime);

    // Run electrical subsolver to compute node voltages and branch currents
    solve_electrical(build_result_->electrical.plan, build_result_->electrical.runtime.element_value_a, state_, build_result_->electrical.runtime, dt);

    // Execute pass for solver-owned components that need post-solve computation
    // (e.g., AZS thermal model reads branch currents from solved electrical state).
    run_solver_owned_ops(build_result_->electrical.execute_ops, state_, dt);

    // == Hydraulic domain solve ==
    if (!build_result_->hydraulic.plan.islands.empty()) {
        state_.hydraulic_rt = &build_result_->hydraulic.runtime;
        jit_solver_impl::build_common::init_element_values_from_plan(build_result_->hydraulic.plan, build_result_->hydraulic.runtime);
        update_hydraulic_dynamic_sources(*build_result_, state_, build_result_->hydraulic.runtime);
        solve_hydraulic(build_result_->hydraulic.plan, build_result_->hydraulic.runtime.element_value_a, state_, build_result_->hydraulic.runtime, dt);
        run_solver_owned_ops(build_result_->hydraulic.execute_ops, state_, dt);
    }

    build_result_->scheduler.step(state_, dt);

    // Explicit commit pass for solver-owned components (Battery discharge, etc).
    // These are NOT in the scheduler's source/consumer lists but need per-frame
    // commit to update internal state (e.g., Battery.charge).
    run_solver_owned_ops(build_result_->electrical.commit_ops, state_, dt);

    // Hydraulic commit pass
    if (!build_result_->hydraulic.plan.islands.empty()) {
        run_solver_owned_ops(build_result_->hydraulic.commit_ops, state_, dt);
    }

    // Guard destructor clears state_.electrical_rt = nullptr here.

    time_ += dt;
    step_count_++;
}

template <typename SolverTag>
float Simulator<SolverTag>::get_signal_value(core::InternedId key) const {
    if (!build_result_.has_value() || key.empty()) {
        return 0.0f;
    }
    auto it = build_result_->port_to_signal.find(key);
    if (it != build_result_->port_to_signal.end() && it->second < state_.values.size()) {
        return state_.values[it->second];
    }
    return 0.0f;
}

template <typename SolverTag>
void Simulator<SolverTag>::apply_typed_overrides(
    const std::vector<std::pair<core::InternedId, float>>& overrides) {
    if (!build_result_.has_value()) {
        return;
    }
    for (const auto& [key, value] : overrides) {
        if (key.empty()) continue;
        auto it = build_result_->port_to_signal.find(key);
        if (it != build_result_->port_to_signal.end() && it->second < state_.values.size()) {
            state_.values[it->second] = value;
        }
    }
}

template <typename SolverTag>
const core::StringInterner& Simulator<SolverTag>::signal_key_interner() const {
    if (build_result_.has_value()) {
        return build_result_->signal_key_interner;
    }
    static const core::StringInterner empty;
    return empty;
}

template <typename SolverTag>
core::InternedId Simulator<SolverTag>::resolve_signal_key(
    std::string_view node_id, std::string_view port_name) const {
    if (!build_result_.has_value()) return {};
    const std::string key = signal_key::make_node_port_key(node_id, port_name);
    return build_result_->signal_key_interner.lookup(key);
}

template class Simulator<JIT_Solver>;
