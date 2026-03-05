#include "simulation.h"
#include "persist.h"
#include "json_parser/json_parser.h"
#include <cmath>

using namespace an24;

void SimulationController::build(const Blueprint& bp) {
    // Convert blueprint to JSON, then parse as simulator format
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

    running = false;
    time = 0.0f;
    step_count = 0;
}

void SimulationController::step(float dt) {
    if (!build_result.has_value()) return;

    state.clear_through();
    build_result->systems.solve_step(state, step_count);
    state.precompute_inv_conductance();

    // SOR update
    for (size_t i = 0; i < state.across.size(); ++i) {
        if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
            state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
        }
    }

    build_result->systems.post_step(state, dt);
    time += dt;
    step_count++;
}

float SimulationController::get_wire_voltage(const std::string& port_name) const {
    if (!build_result.has_value()) return 0.0f;

    auto it = build_result->port_to_signal.find(port_name);
    if (it == build_result->port_to_signal.end()) return 0.0f;

    if (it->second >= state.across.size()) return 0.0f;
    return state.across[it->second];
}

float SimulationController::get_port_value(const std::string& node_id, const std::string& port_name) const {
    return get_wire_voltage(node_id + "." + port_name);
}

bool SimulationController::wire_is_energized(const std::string& port_name, float threshold) const {
    float v = get_wire_voltage(port_name);
    return std::abs(v) > threshold;
}
