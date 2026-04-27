#include "simulator.h"
#include "build_common.h"
#include "core/solvers/common/nodal_patch_ops.h"
#include "core/solvers/common/signal_key.h"
#include "../../../parse_number.h"
#include <algorithm>
#include <cmath>

namespace {

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

    // Initialize element_value_a from plan defaults — once, at start.
    // These arrays are mutated in-place by patch ops each frame but their
    // sizing and initial values never change after build.
    jit_solver_impl::build_common::init_element_values_from_plan(
        build_result_->electrical.plan, build_result_->electrical.runtime);
    jit_solver_impl::build_common::init_element_values_from_plan(
        build_result_->hydraulic.plan, build_result_->hydraulic.runtime);
    jit_solver_impl::build_common::init_element_values_from_plan(
        build_result_->pneumatic.plan, build_result_->pneumatic.runtime);

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
    run_solver_owned_ops(build_result_->hydraulic.execute_ops, state_, 0.0);

    // Bootstrap pneumatic commit + execute ops (same pattern).
    run_solver_owned_ops(build_result_->pneumatic.commit_ops, state_, 0.0);
    run_solver_owned_ops(build_result_->pneumatic.execute_ops, state_, 0.0);

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
    state_.pneumatic_rt = nullptr;

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
    dt = std::min(dt, MAX_DT);

    // ===========================================================================
    // Simulation pipeline (E-009: single-solve architecture)
    // ===========================================================================
    //
    // The pipeline uses ONE electrical solve per frame with one-frame-delayed
    // actuator states. This is intentional for a game at 60Hz+:
    //
    //   1. update_dynamic_sources — stamp actuator states from PREVIOUS frame
    //   2. solve_nodal — single Gaussian solve for all islands
    //   3. execute solver-owned — post-solve compute (e.g., AZS thermal model)
    //   4. scheduler.step — execute all logical/mechanical/etc. components
    //   5. commit solver-owned — battery discharge, state transitions
    //
    // One-frame delay semantics: actuator state changes take effect in the
    // NEXT frame's solve, not the current one.
    // ===========================================================================

    // Set pointer before solver runs so components can access electrical_rt.
    // RAII guard ensures cleanup on ALL exit paths.
    state_.electrical_rt = &build_result_->electrical.runtime;
    struct RtGuard {
        SimulationState& st;
        ~RtGuard() { st.electrical_rt = nullptr; st.hydraulic_rt = nullptr; st.pneumatic_rt = nullptr; }
    } guard{state_};

    // == Electrical domain solve ==
    update_nodal_dynamic_sources(
        build_result_->electrical.patch_ops, state_, build_result_->electrical.runtime);
    solve_nodal(build_result_->electrical.plan,
                build_result_->electrical.runtime.element_value_a,
                state_, build_result_->electrical.runtime, dt);
    run_solver_owned_ops(build_result_->electrical.execute_ops, state_, dt);

    // == Hydraulic domain solve ==
    if (!build_result_->hydraulic.plan.islands.empty()) {
        state_.hydraulic_rt = &build_result_->hydraulic.runtime;
        update_nodal_dynamic_sources(
            build_result_->hydraulic.patch_ops, state_, build_result_->hydraulic.runtime);
        solve_nodal(build_result_->hydraulic.plan,
                    build_result_->hydraulic.runtime.element_value_a,
                    state_, build_result_->hydraulic.runtime, dt);
        run_solver_owned_ops(build_result_->hydraulic.execute_ops, state_, dt);
    }

    // == Pneumatic domain solve ==
    if (!build_result_->pneumatic.plan.islands.empty()) {
        state_.pneumatic_rt = &build_result_->pneumatic.runtime;
        update_nodal_dynamic_sources(
            build_result_->pneumatic.patch_ops, state_, build_result_->pneumatic.runtime);
        solve_nodal(build_result_->pneumatic.plan,
                    build_result_->pneumatic.runtime.element_value_a,
                    state_, build_result_->pneumatic.runtime, dt);
        run_solver_owned_ops(build_result_->pneumatic.execute_ops, state_, dt);
    }

    build_result_->scheduler.step(state_, dt);

    // Explicit commit pass for solver-owned components (Battery discharge, etc).
    run_solver_owned_ops(build_result_->electrical.commit_ops, state_, dt);

    // Hydraulic commit pass
    if (!build_result_->hydraulic.plan.islands.empty()) {
        run_solver_owned_ops(build_result_->hydraulic.commit_ops, state_, dt);
    }

    // Pneumatic commit pass
    if (!build_result_->pneumatic.plan.islands.empty()) {
        run_solver_owned_ops(build_result_->pneumatic.commit_ops, state_, dt);
    }

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
