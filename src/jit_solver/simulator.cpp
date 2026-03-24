#include "simulator.h"
#include "scheduling.h"
#include "../json_parser/json_parser.h"
#include "../parse_number.h"
#include "components/port_registry.h"
#include "components/all.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>

template<typename SolverTag>
Simulator<SolverTag>::Simulator(Simulator&& other) noexcept
    : build_result_(std::move(other.build_result_))
    , state_(std::move(other.state_))
    , running_(other.running_)
    , time_(other.time_)
    , step_count_(other.step_count_)
    , omega_(other.omega_)
    , prev_convergence_error_(other.prev_convergence_error_)
    , adaptive_omega_enabled_(other.adaptive_omega_enabled_)
    , accumulator_mechanical_(other.accumulator_mechanical_)
    , accumulator_hydraulic_(other.accumulator_hydraulic_)
    , accumulator_thermal_(other.accumulator_thermal_)
{
    other.running_ = false;
    other.time_ = 0.0f;
    other.step_count_ = 0;
    other.omega_ = SOR::OMEGA;
    other.prev_convergence_error_ = 0.0f;
    other.accumulator_mechanical_ = 0.0f;
    other.accumulator_hydraulic_ = 0.0f;
    other.accumulator_thermal_ = 0.0f;
}

template<typename SolverTag>
Simulator<SolverTag>& Simulator<SolverTag>::operator=(Simulator&& other) noexcept {
    if (this != &other) {
        stop();  // Clean up current resources

        build_result_ = std::move(other.build_result_);
        state_ = std::move(other.state_);
        running_ = other.running_;
        time_ = other.time_;
        step_count_ = other.step_count_;
        omega_ = other.omega_;
        prev_convergence_error_ = other.prev_convergence_error_;
        adaptive_omega_enabled_ = other.adaptive_omega_enabled_;
        accumulator_mechanical_ = other.accumulator_mechanical_;
        accumulator_hydraulic_ = other.accumulator_hydraulic_;
        accumulator_thermal_ = other.accumulator_thermal_;

        other.running_ = false;
        other.time_ = 0.0f;
        other.step_count_ = 0;
        other.omega_ = SOR::OMEGA;
        other.prev_convergence_error_ = 0.0f;
        other.accumulator_mechanical_ = 0.0f;
        other.accumulator_hydraulic_ = 0.0f;
        other.accumulator_thermal_ = 0.0f;
    }
    return *this;
}

template<typename SolverTag>
void Simulator<SolverTag>::start_from_json(const std::string& json_str) {
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
                value = locale_safe::parse_float_or(it_val->second, 0.0f);
            }
            auto it_sig = build_result_->port_to_signal.find(dev.name + ".v");
            if (it_sig != build_result_->port_to_signal.end()) {
                state_.across[it_sig->second] = value;
            }
        }
    }

    // Allocate convergence buffer for diagnostics
    state_.resize_buffers(build_result_->signal_count);

    // Move LUT arena from build result to simulation state
    state_.lut_keys = std::move(build_result_->lut_keys);
    state_.lut_values = std::move(build_result_->lut_values);

    // Reset time, step count, and accumulators
    time_ = 0.0f;
    step_count_ = 0;
    omega_ = SOR::OMEGA;
    prev_convergence_error_ = 0.0f;
    accumulator_mechanical_ = 0.0f;
    accumulator_hydraulic_ = 0.0f;
    accumulator_thermal_ = 0.0f;

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
    omega_ = SOR::OMEGA;
    prev_convergence_error_ = 0.0f;
    accumulator_mechanical_ = 0.0f;
    accumulator_hydraulic_ = 0.0f;
    accumulator_thermal_ = 0.0f;

    // Mark as not running
    running_ = false;
}

template<typename SolverTag>
void Simulator<SolverTag>::step(float dt) {
    if (!running_ || !build_result_.has_value()) return;

    state_.clear_through();

    // Accumulate dt for sub-rate domains (FPS-independent physics)
    accumulator_mechanical_ += dt;
    accumulator_hydraulic_ += dt;
    accumulator_thermal_ += dt;

    // Data-oriented multi-domain solving with zero branching
    // Components are pre-sorted by domain, so we just iterate the relevant vectors

    // Electrical: every step
    for (auto* variant : build_result_->domain_components.electrical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_electrical(state_, dt); }) {
                comp.solve_electrical(state_, dt);
            }
        }, *variant);
    }

    // Mechanical: every 3rd step — use accumulated dt, then reset
    if ((step_count_ % DomainSchedule::MECHANICAL_PERIOD) == 0) {
        for (auto* variant : build_result_->domain_components.mechanical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_mechanical(state_, accumulator_mechanical_); }) {
                    comp.solve_mechanical(state_, accumulator_mechanical_);
                }
            }, *variant);
        }
        accumulator_mechanical_ = 0.0f;
    }

    // Hydraulic: every 12th step — use accumulated dt, then reset
    if ((step_count_ % DomainSchedule::HYDRAULIC_PERIOD) == 0) {
        for (auto* variant : build_result_->domain_components.hydraulic) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_hydraulic(state_, accumulator_hydraulic_); }) {
                    comp.solve_hydraulic(state_, accumulator_hydraulic_);
                }
            }, *variant);
        }
        accumulator_hydraulic_ = 0.0f;
    }

    // Thermal: every 60th step — use accumulated dt, then reset
    if ((step_count_ % DomainSchedule::THERMAL_PERIOD) == 0) {
        for (auto* variant : build_result_->domain_components.thermal) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_thermal(state_, accumulator_thermal_); }) {
                    comp.solve_thermal(state_, accumulator_thermal_);
                }
            }, *variant);
        }
        accumulator_thermal_ = 0.0f;
    }

    // SOR solver - multiple cheap sweeps per step for improved stability
    state_.precompute_inv_conductance();
    state_.save_convergence_state();

    for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {
        solve_sor_iteration(
            state_.across.data(),
            state_.through.data(),
            state_.inv_conductance.data(),
            state_.dynamic_signals_count,
            omega_
        );
    }

    // Optional adaptive omega: reduce fast on worsening, recover slowly on improvement
    float err = state_.get_max_change();
    if (adaptive_omega_enabled_ && prev_convergence_error_ > 0.0f) {
        if (err > prev_convergence_error_ * ::SORAdaptive::ERROR_WORSE_FACTOR) {
            omega_ = std::max(::SORAdaptive::OMEGA_MIN, omega_ * ::SORAdaptive::OMEGA_DOWNSCALE);
        } else if (err < prev_convergence_error_ * ::SORAdaptive::ERROR_BETTER_FACTOR) {
            omega_ = std::min(SOR::OMEGA, omega_ * ::SORAdaptive::OMEGA_UPSCALE);
        }
    }
    prev_convergence_error_ = err;

    // post_step for components that need it
    for (auto& [name, variant] : build_result_->devices) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.post_step(state_, dt); }) {
                comp.post_step(state_, dt);
            }
        }, variant);
    }

    // Logical: after SOR+post_step so logical components read converged
    // electrical values and their outputs are final (SOR does not touch them)
    for (auto* variant : build_result_->domain_components.logical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_logical(state_, dt); }) {
                comp.solve_logical(state_, dt);
            }
        }, *variant);
    }

    // DEBUG: Log every 60 steps
    time_ += dt;
    step_count_++;
}

template<typename SolverTag>
float Simulator<SolverTag>::get_wire_voltage(const std::string& port_name) const {
    if (!build_result_.has_value()) return 0.0f;

    auto it = build_result_->port_to_signal.find(port_name);
    if (it == build_result_->port_to_signal.end()) {
        // Sub-blueprint port lookup: editor queries "sbi_id.expose_name"
        // while simulator keys are "sbi_id:expose_name.ext".
        auto dot = port_name.find('.');
        if (dot != std::string::npos) {
            std::string fallback = port_name.substr(0, dot) + ":" +
                                   port_name.substr(dot + 1) + ".ext";
            it = build_result_->port_to_signal.find(fallback);
            if (it == build_result_->port_to_signal.end()) return 0.0f;
        } else {
            return 0.0f;
        }
    }

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
