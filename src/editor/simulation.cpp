#include "simulation.h"
#include "persist.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <fstream>
#include <filesystem>

using namespace an24;

/// Expand collapsed blueprint nodes (replaces them with flattened devices)
static Blueprint expand_blueprints(const Blueprint& input) {
    Blueprint expanded = input;
    expanded.nodes.clear();
    expanded.wires.clear();

    // Copy non-blueprint nodes
    for (const auto& node : input.nodes) {
        if (node.kind != NodeKind::Blueprint) {
            expanded.add_node(node);
        }
    }

    // Expand blueprint nodes
    for (const auto& node : input.nodes) {
        if (node.kind == NodeKind::Blueprint) {
            spdlog::info("[sim] expanding collapsed blueprint: {} ({})",
                       node.type_name, node.blueprint_path);

            // Load the nested blueprint JSON
            // Try to find the blueprint file (same logic as JSON parser)
            std::filesystem::path bp_path(node.blueprint_path);
            if (!std::filesystem::exists(bp_path) && bp_path.is_relative()) {
                // Try common relative paths from build directory
                std::vector<std::filesystem::path> try_paths = {
                    bp_path,
                    "../" / bp_path,
                    "../../" / bp_path,
                    "../../../" / bp_path,
                };
                for (const auto& path : try_paths) {
                    if (std::filesystem::exists(path)) {
                        bp_path = path;
                        break;
                    }
                }
            }

            if (!std::filesystem::exists(bp_path)) {
                spdlog::error("[sim] blueprint file not found: {}", node.blueprint_path);
                continue;
            }

            std::ifstream file(bp_path);
            if (!file.is_open()) {
                spdlog::error("[sim] failed to open blueprint: {}", node.blueprint_path);
                continue;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            // Parse nested blueprint (triggers blueprint loading fallback)
            an24::ParserContext nested_ctx;
            try {
                nested_ctx = an24::parse_json(content);
            } catch (const std::exception& e) {
                spdlog::error("[sim] failed to parse blueprint {}: {}",
                           node.blueprint_path, e.what());
                continue;
            }

            // Add all nested devices with prefix
            std::string prefix = node.id;  // Use collapsed node's ID as prefix

            for (const auto& dev : nested_ctx.devices) {
                Node nested_node;
                nested_node.id = prefix + ":" + dev.name;
                nested_node.name = nested_node.id;
                nested_node.type_name = dev.classname;
                nested_node.kind = NodeKind::Node;
                nested_node.collapsed = false;
                nested_node.pos = node.pos;  // All nested devices at same position for now

                // Copy params from DeviceInstance (for Battery, RefNode, etc.)
                nested_node.params = dev.params;

                // Get size from component registry
                nested_node.size = get_default_node_size(dev.classname, nullptr);

                // Add ports
                for (const auto& [port_name, port] : dev.ports) {
                    if (port.direction == PortDirection::In || port.direction == PortDirection::InOut) {
                        nested_node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
                    }
                    if (port.direction == PortDirection::Out || port.direction == PortDirection::InOut) {
                        nested_node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
                    }
                }

                expanded.add_node(nested_node);
            }

            // Rewrite connections with prefix
            for (const auto& conn : nested_ctx.connections) {
                Wire wire;
                wire.id = expanded.next_wire_id++;

                // Rewrite: "vin.port" -> "lamp_pass_through_1:vin.port"
                wire.start.node_id = prefix + ":" + conn.from.substr(0, conn.from.find('.'));
                wire.start.port_name = conn.from.substr(conn.from.find('.') + 1);
                wire.end.node_id = prefix + ":" + conn.to.substr(0, conn.to.find('.'));
                wire.end.port_name = conn.to.substr(conn.to.find('.') + 1);

                expanded.add_wire(wire);
            }

            // Now connect parent's wires to the expanded devices
            // Parent connects to "lamp_pass_through_1.vin" → should connect to "lamp_pass_through_1:vin.port"
            // Parent connects from "lamp_pass_through_1.vout" → should connect from "lamp_pass_through_1:vout.port"
            // We need to rewrite parent connections to point to the correct BlueprintInput/BlueprintOutput devices

            spdlog::info("[sim] expanded blueprint: {} → {} devices, {} connections",
                       node.id, nested_ctx.devices.size(), nested_ctx.connections.size());
        }
    }

    // Rewrite connections that point to collapsed blueprint nodes
    for (const auto& wire : input.wires) {
        Wire new_wire = wire;
        new_wire.id = expanded.next_wire_id++;

        // Check if start node is a collapsed blueprint
        bool start_is_blueprint = false;
        for (const auto& node : input.nodes) {
            if (node.kind == NodeKind::Blueprint && node.id == wire.start.node_id) {
                // Rewrite to connect to BlueprintInput inside
                // "collapsed_id.vin" → "collapsed_id:vin.port"
                new_wire.start.node_id = node.id + ":" + wire.start.port_name;
                new_wire.start.port_name = "port";
                start_is_blueprint = true;
                break;
            }
        }

        // Check if end node is a collapsed blueprint
        bool end_is_blueprint = false;
        for (const auto& node : input.nodes) {
            if (node.kind == NodeKind::Blueprint && node.id == wire.end.node_id) {
                // Rewrite to connect to BlueprintOutput inside
                // "collapsed_id.vout" → "collapsed_id:vout.port"
                new_wire.end.node_id = node.id + ":" + wire.end.port_name;
                new_wire.end.port_name = "port";
                end_is_blueprint = true;
                break;
            }
        }

        expanded.add_wire(new_wire);
    }

    // Copy non-blueprint wires
    // (already done above in the rewrite loop)

    return expanded;
}

void SimulationController::build(const Blueprint& bp) {
    // Expand collapsed blueprint nodes before building
    Blueprint expanded = expand_blueprints(bp);

    // Convert blueprint to JSON, then parse as simulator format
    std::string json_str = blueprint_to_json(expanded);
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

    for (auto* variant : build_result->domain_components.logical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_logical(state, dt); }) {
                comp.solve_logical(state, dt);
            }
        }, *variant);
    }

    // Mechanical: every 3rd step (20 Hz)
    if ((step_count % 3) == 0) {
        for (auto* variant : build_result->domain_components.mechanical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_mechanical(state, dt); }) {
                    comp.solve_mechanical(state, dt);
                }
            }, *variant);
        }
    }

    // Hydraulic: every 12th step (5 Hz)
    if ((step_count % 12) == 0) {
        for (auto* variant : build_result->domain_components.hydraulic) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_hydraulic(state, dt); }) {
                    comp.solve_hydraulic(state, dt);
                }
            }, *variant);
        }
    }

    // Thermal: every 60th step (1 Hz)
    if ((step_count % 60) == 0) {
        for (auto* variant : build_result->domain_components.thermal) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_thermal(state, dt); }) {
                    comp.solve_thermal(state, dt);
                }
            }, *variant);
        }
    }

    state.precompute_inv_conductance();

    // DEBUG: Log conductance after solve
    if (step_count == 0) {
        spdlog::warn("[sim] AFTER solve: conductance[0]={:.6f}", state.conductance[0]);
    }

    // SOR update
    for (size_t i = 0; i < state.across.size(); ++i) {
        if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
            state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
        }
    }

    // post_step for components that need it
    for (auto& [name, variant] : build_result->devices) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.post_step(state, dt); }) {
                comp.post_step(state, dt);
            }
        }, variant);
    }

    // DEBUG: Log every step
    if (step_count == 0 || step_count % 60 == 0) {
        auto bat_it = build_result->port_to_signal.find("bat1.v_out");
        if (bat_it != build_result->port_to_signal.end()) {
            spdlog::warn("[sim] Step {}: bat1.v_out = {:.2f}V", step_count, state.across[bat_it->second]);
        }
    }

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

void SimulationController::apply_overrides(const std::unordered_map<std::string, float>& overrides) {
    if (!build_result.has_value()) return;

    for (const auto& [port_ref, value] : overrides) {
        auto it = build_result->port_to_signal.find(port_ref);
        if (it != build_result->port_to_signal.end()) {
            uint32_t signal_idx = it->second;
            if (signal_idx < state.across.size()) {
                state.across[signal_idx] = value;
            }
        }
    }
}

void SimulationController::reset() {
    // Stop simulation
    running = false;

    // Reset time
    time = 0.0f;
    step_count = 0;

    // Reset all signals to 0
    if (!build_result.has_value()) return;

    // Re-allocate state (clears all arrays to 0)
    state = SimulationState();

    // Re-allocate signals (same as in build)
    for (uint32_t i = 0; i < build_result->signal_count; ++i) {
        bool is_fixed = std::binary_search(
            build_result->fixed_signals.begin(),
            build_result->fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Note: Unlike build(), we do NOT initialize RefNodes here
    // All signals stay at 0 - this is the expected behavior for Stop
}
