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
    auto slots = build_result_->nodal_slots();
    for (auto& slot : slots) {
        jit_solver_impl::build_common::init_element_values_from_plan(
            slot.artifacts.plan, slot.artifacts.runtime);
    }

    // Bootstrap: run solver-owned commit+execute ops for all domains.
    // Commit ops write initial component state to signals (e.g., AZS/Relay
    // write closed=true). Execute ops compute initial source values
    // (e.g., FuelTank gravity pressure → p_source signal).
    // Electrical execute ops are safe no-ops here (electrical_rt is null).
    for (auto& slot : slots) {
        run_solver_owned_ops(slot.artifacts.commit_ops, state_, 0.0);
        run_solver_owned_ops(slot.artifacts.execute_ops, state_, 0.0);
    }

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

    // Null all rt pointers (safety: ensure no stale pointers survive rebuild).
    for (auto& slot : slots) {
        state_.*(slot.rt_member) = nullptr;
    }

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
    // The pipeline uses ONE solve per domain per frame with one-frame-delayed
    // actuator states. This is intentional for a game at 60Hz+:
    //
    //   1. update_dynamic_sources — stamp actuator states from PREVIOUS frame
    //   2. solve_nodal — Gaussian solve for all islands in each domain
    //   3. execute solver-owned — post-solve compute (e.g., AZS thermal model)
    //   4. scheduler.step — execute all logical/mechanical/etc. components
    //   5. commit solver-owned — battery discharge, state transitions
    //
    // One-frame delay semantics: actuator state changes take effect in the
    // NEXT frame's solve, not the current one.
    // ===========================================================================

    auto slots = build_result_->nodal_slots();

    // RAII guard: null all rt pointers on every exit path.
    struct RtGuard {
        SimulationState& st;
        std::array<NodalSlot, NODAL_DOMAIN_COUNT> slots;
        ~RtGuard() {
            for (const auto& slot : slots) {
                st.*(slot.rt_member) = nullptr;
            }
        }
    } guard{state_, slots};

    // == Solve + Execute pass (all domains) ==
    // Empty islands → empty patch ops, empty solve, empty execute ops → no-op.
    // Setting the rt pointer unconditionally is safe: components always pair
    // the rt-null check with is_valid(handle).
    for (auto& slot : slots) {
        state_.*(slot.rt_member) = &slot.artifacts.runtime;
        update_nodal_dynamic_sources(
            slot.artifacts.patch_ops, state_, slot.artifacts.runtime);
        solve_nodal(
            slot.artifacts.plan,
            slot.artifacts.runtime.element_value_a,
            state_, slot.artifacts.runtime, dt);
        run_solver_owned_ops(slot.artifacts.execute_ops, state_, dt);
    }

    // == Scheduler pass (logical, mechanical, etc.) ==
    build_result_->scheduler.step(state_, dt);

    // == Commit pass (all domains) ==
    for (auto& slot : slots) {
        run_solver_owned_ops(slot.artifacts.commit_ops, state_, dt);
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
