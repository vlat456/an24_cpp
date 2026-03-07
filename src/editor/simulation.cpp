#include "simulation.h"
#include "visual/scene/persist.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace an24;

void SimulationController::build(const Blueprint& bp) {
    // With always-flatten architecture, blueprints are already expanded when added
    // No need to expand here - just convert to JSON and parse
    std::string json_str = blueprint_to_json(bp);
    auto ctx = parse_json(json_str);

    // Build systems from parsed context
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    build_result = build_systems_dev(ctx.devices, connections);

    // Reset state before allocating (critical for rebuild)
    state = SimulationState();

    // Allocate signals
    for (uint32_t i = 0; i < build_result->signal_count; ++i) {
        bool is_fixed = std::binary_search(
            build_result->fixed_signals.begin(),
            build_result->fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize fixed signals from RefNodes
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            auto it_sig = build_result->port_to_signal.find(dev.name + ".v");
            if (it_sig != build_result->port_to_signal.end()) {
                state.across[it_sig->second] = value;
            }
        }
    }

    // Preserve running state across rebuilds (don't reset running flag)
    time = 0.0f;
    step_count = 0;
}

void SimulationController::step(float dt) {
    if (!build_result.has_value()) return;

    // DEBUG: Log first step
    if (step_count == 0) {
        spdlog::warn("[sim] ===== FIRST STEP =====");
        spdlog::warn("[sim] {} devices, dt={}, signals={}",
            build_result->devices.size(), dt, state.across.size());

        // Log battery voltage before solve
        auto bat_it = build_result->port_to_signal.find("bat1.v_out");
        if (bat_it != build_result->port_to_signal.end()) {
            spdlog::warn("[sim] BEFORE: bat1.v_out = {:.2f}V", state.across[bat_it->second]);
        }
    }

    state.clear_through();

    // Data-oriented multi-domain solving with zero branching
    // Components are pre-sorted by domain, so we just iterate the relevant vectors

    // Electrical/Logical: every step (60 Hz)
    for (auto* variant : build_result->domain_components.electrical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_electrical(state, dt); }) {
                comp.solve_electrical(state, dt);
            }
        }, *variant);
    }

    state.precompute_inv_conductance();

    // SOR solver (electrical network relaxation)
    for (size_t i = 0; i < state.across.size(); ++i) {
        if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
            state.across[i] += state.through[i] * state.inv_conductance[i] * 1.8f;
        }
    }

    // Post-step (update device state)
    for (auto& [name, variant] : build_result->devices) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.post_step(state, dt); }) {
                comp.post_step(state, dt);
            }
        }, variant);
    }

    time += dt;
    step_count++;
}

float SimulationController::get_port_value(const std::string& device_id, const std::string& port_name) const {
    return get_wire_voltage(device_id + "." + port_name);
}

void SimulationController::reset() {
    build_result.reset();
    state = SimulationState();
    time = 0.0f;
    step_count = 0;
    running = false;
}

float SimulationController::get_wire_voltage(const std::string& port_name) const {
    if (!build_result.has_value()) return 0.0f;
    auto it = build_result->port_to_signal.find(port_name);
    if (it == build_result->port_to_signal.end()) {
        // Blueprint port fallback: "node_id.port_name" → "node_id:port_name.ext"
        auto dot = port_name.find('.');
        if (dot != std::string::npos) {
            std::string fallback = port_name.substr(0, dot) + ":" +
                                   port_name.substr(dot + 1) + ".ext";
            it = build_result->port_to_signal.find(fallback);
            if (it == build_result->port_to_signal.end()) return 0.0f;
        } else {
            return 0.0f;
        }
    }
    return state.across[it->second];
}

bool SimulationController::wire_is_energized(const std::string& port_name, float threshold) const {
    return std::abs(get_wire_voltage(port_name)) > threshold;
}
