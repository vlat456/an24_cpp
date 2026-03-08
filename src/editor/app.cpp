#include "app.h"
#include "visual/node/node.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

void EditorApp::update_node_content_from_simulation() {
    if (!simulation_running) return;

    for (auto& node : scene.nodes()) {
        // Update Voltmeter gauge voltage
        if (node.type_name == "Voltmeter") {
            float voltage = simulation.get_port_value(node.id, "v_in");
            node.node_content.value = voltage;
        }
        // Update IndicatorLight text based on brightness
        else if (node.type_name == "IndicatorLight") {
            float brightness = simulation.get_port_value(node.id, "brightness");
            node.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
        }
        // Update DMR400 state
        else if (node.type_name == "DMR400") {
            float v_gen = simulation.get_port_value(node.id, "v_gen");
            float v_bus = simulation.get_port_value(node.id, "v_bus");
            // Relay is closed (ON) when generator voltage is higher than bus
            // This is simplified - actual logic has hysteresis
            bool connected = v_gen > v_bus + 2.0f;
            node.node_content.state = connected;
        }
        // Update Switch toggle state from state port (1.0V = closed, 0.0V = open)
        else if (node.type_name == "Switch") {
            float state_voltage = simulation.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update HoldButton state from state port (1.0V = pressed, 0.0V = released/idle)
        else if (node.type_name == "HoldButton") {
            float state_voltage = simulation.get_port_value(node.id, "state");
            // state=true = PRESSED, state=false = RELEASED/idle
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Relay has no UI - automatic device controlled by external signals
    }
}

// Reset node_content to default values (used when simulation stops)
void EditorApp::reset_node_content() {
    using namespace an24;

    for (auto& node : scene.nodes()) {
        const auto* def = component_registry.get(node.type_name);
        if (!def) continue;

        // Reset IndicatorLight to OFF
        if (node.type_name == "IndicatorLight") {
            node.node_content.label = "OFF";
        }
        // Reset Voltmeter gauge to 0
        else if (node.type_name == "Voltmeter") {
            node.node_content.value = 0.0f;
        }
        // Reset DMR400 to disconnected state
        else if (node.type_name == "DMR400") {
            node.node_content.state = false;
        }
        // Reset Switch to default closed state
        else if (node.type_name == "Switch") {
            auto it = def->default_params.find("closed");
            if (it != def->default_params.end()) {
                node.node_content.state = (it->second == "true");
            } else {
                node.node_content.state = false;
            }
        }
        // Reset HoldButton to RELEASED
        else if (node.type_name == "HoldButton") {
            node.node_content.label = "RELEASED";
            node.node_content.state = false;
        }
    }
}

// [DRY-i9j0] Replaced duplicate — now delegates to create_node_content_from_def in node.h
static NodeContent create_node_content(const an24::ComponentDefinition* def) {
    return create_node_content_from_def(def);
}

void EditorApp::add_component(const std::string& classname, Pt world_pos, const std::string& group_id) {
    using namespace an24;
    
    // Check if component exists in registry
    if (!component_registry.has(classname)) {
        printf("Error: Unknown component classname '%s'\n", classname.c_str());
        return;
    }
    
    const auto* def = component_registry.get(classname);
    if (!def) {
        printf("Error: Component definition not found for '%s'\n", classname.c_str());
        return;
    }
    
    // Generate unique ID
    int counter = 1;
    std::string base_id = classname;
    // Convert to lowercase for ID (e.g., "Battery" -> "battery")
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);
    
    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (scene.findNode(unique_id.c_str()) != nullptr);
    
    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, scene.gridStep());
    
    // Create node
    Node node;
    node.id = unique_id;
    node.name = unique_id;  // Use ID as display name for now
    node.type_name = classname;
    node.pos = snapped_pos;
    node.group_id = group_id;

    // Set NodeKind based on classname
    if (classname == "Bus") {
        node.kind = NodeKind::Bus;
    } else if (classname == "RefNode") {
        node.kind = NodeKind::Ref;
    } else {
        node.kind = NodeKind::Node;
    }

    // Get size from component definition (single source of truth)
    node.size = get_default_node_size(classname, &component_registry);
    
    // Add ports from component definition
    for (const auto& [port_name, port_def] : def->default_ports) {
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(port_name.c_str(), PortSide::Input);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(port_name.c_str(), PortSide::Output);
        } else if (port_def.direction == PortDirection::InOut) {
            // InOut ports go to both inputs and outputs
            node.inputs.emplace_back(port_name.c_str(), PortSide::InOut);
            node.outputs.emplace_back(port_name.c_str(), PortSide::InOut);
        }
    }

    // Create node_content from component definition (no more hardcoded component lists!)
    node.node_content = create_node_content(def);

    // Add node to scene
    scene.addNode(node);

    // Keep collapsed_groups.internal_node_ids in sync
    if (!group_id.empty()) {
        for (auto& g : blueprint.collapsed_groups) {
            if (g.id == group_id) {
                g.internal_node_ids.push_back(unique_id);
                break;
            }
        }
    }

    // Rebuild simulation to include new component
    rebuild_simulation();

    printf("Added component: %s (id=%s) at (%.1f, %.1f) group=%s\n",
           classname.c_str(), unique_id.c_str(), snapped_pos.x, snapped_pos.y,
           group_id.empty() ? "root" : group_id.c_str());
}

void EditorApp::scan_blueprints() {
    namespace fs = std::filesystem;

    // Try to find blueprints/ directory (same resolution logic as parse_json)
    std::vector<fs::path> try_paths = {
        "blueprints/",
        "../blueprints/",
        "../../blueprints/",
    };

    fs::path blueprints_dir;
    bool found = false;
    for (const auto& path : try_paths) {
        if (fs::is_directory(path)) {
            blueprints_dir = path;
            found = true;
            break;
        }
    }

    if (!found) {
        spdlog::warn("[editor] blueprints/ directory not found");
        return;
    }

    // Scan for .json files
    blueprints.clear();
    for (const auto& entry : fs::directory_iterator(blueprints_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string blueprint_name = entry.path().stem().string();
            std::string blueprint_path = entry.path().string();

            // Try to load blueprint and extract exposed ports
            try {
                std::ifstream file(entry.path());
                if (!file.is_open()) continue;

                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
                file.close();

                an24::ParserContext ctx = an24::parse_json(content);
                auto exposed_ports = an24::extract_exposed_ports(ctx);

                BlueprintInfo info;
                info.name = blueprint_name;
                info.path = blueprint_path;
                info.exposed_ports = exposed_ports;

                blueprints.push_back(std::move(info));

                spdlog::info("[editor] discovered blueprint: {} ({} exposed ports)",
                           blueprint_name, exposed_ports.size());
            } catch (const std::exception& e) {
                spdlog::warn("[editor] failed to load blueprint {}: {}",
                           blueprint_name, e.what());
            }
        }
    }

    // Sort alphabetically
    std::sort(blueprints.begin(), blueprints.end(),
              [](const BlueprintInfo& a, const BlueprintInfo& b) {
                  return a.name < b.name;
              });

    spdlog::info("[editor] scanned {} blueprints from {}", blueprints.size(), blueprints_dir.string());
}

void EditorApp::add_blueprint(const std::string& blueprint_name, Pt world_pos, const std::string& group_id) {
    using namespace an24;

    // Find blueprint info
    const BlueprintInfo* info = nullptr;
    for (const auto& bp : blueprints) {
        if (bp.name == blueprint_name) {
            info = &bp;
            break;
        }
    }

    if (!info) {
        spdlog::error("[editor] blueprint not found: {}", blueprint_name);
        return;
    }

    // Generate unique ID
    int counter = 1;
    std::string base_id = blueprint_name;
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);

    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (scene.findNode(unique_id.c_str()) != nullptr);

    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, scene.gridStep());

    // IMMEDIATELY EXPAND the nested blueprint (always-flatten architecture)
    // Load the nested blueprint JSON file
    std::ifstream blueprint_file(info->path);
    if (!blueprint_file.is_open()) {
        spdlog::error("[editor] failed to open blueprint: {}", info->path);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(blueprint_file)),
                        std::istreambuf_iterator<char>());

    // Parse nested blueprint (this will expand any nested blueprints recursively)
    an24::ParserContext nested_ctx;
    try {
        nested_ctx = an24::parse_json(content);
    } catch (const std::exception& e) {
        spdlog::error("[editor] failed to parse blueprint {}: {}",
                     info->path, e.what());
        return;
    }

    // Add all nested devices with prefix to the main blueprint
    std::string prefix = unique_id;  // Use the blueprint's ID as prefix
    std::vector<std::string> internal_node_ids;

    for (const auto& dev : nested_ctx.devices) {
        Node node;
        node.id = prefix + ":" + dev.name;
        node.name = node.id;
        node.type_name = dev.classname;
        // Derive kind from classname (kind is visual, classname is C++ binding)
        if (dev.classname == "Bus") {
            node.kind = NodeKind::Bus;
        } else if (dev.classname == "RefNode") {
            node.kind = NodeKind::Ref;
        } else {
            node.kind = NodeKind::Node;
        }
        node.pos = snapped_pos;  // All start at same position (will be auto-layout)
        node.size = get_default_node_size(dev.classname, &component_registry);

        // Add ports from DeviceInstance
        for (const auto& [port_name, port] : dev.ports) {
            if (port.direction == PortDirection::In || port.direction == PortDirection::InOut) {
                node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
            }
            if (port.direction == PortDirection::Out || port.direction == PortDirection::InOut) {
                node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
            }
        }

        // Add params from DeviceInstance
        node.params = dev.params;

        // Create node_content from ComponentDefinition
        const auto* def = component_registry.get(dev.classname);
        if (def) {
            node.node_content = create_node_content(def);
        }

        scene.blueprint().add_node(node);
        internal_node_ids.push_back(node.id);
    }

    // Add and rewrite connections with prefix
    for (const auto& conn : nested_ctx.connections) {
        Wire wire;
        wire.id = prefix + ":" + conn.from + "->" + prefix + ":" + conn.to;

        // Rewrite from: "vin.port" -> "unique_id:vin.port"
        size_t from_dot = conn.from.find('.');
        if (from_dot != std::string::npos) {
            wire.start.node_id = prefix + ":" + conn.from.substr(0, from_dot);
            wire.start.port_name = conn.from.substr(from_dot + 1);
        }

        // Rewrite to: "vout.port" -> "unique_id:vout.port"
        size_t to_dot = conn.to.find('.');
        if (to_dot != std::string::npos) {
            wire.end.node_id = prefix + ":" + conn.to.substr(0, to_dot);
            wire.end.port_name = conn.to.substr(to_dot + 1);
        }

        scene.blueprint().add_wire(wire);
    }

    // Create COLLAPSED Blueprint node (visible in parent view)
    Node collapsed_node;
    collapsed_node.id = unique_id;
    collapsed_node.name = unique_id;
    collapsed_node.type_name = blueprint_name;
    collapsed_node.kind = NodeKind::Blueprint;
    collapsed_node.collapsed = true;
    collapsed_node.blueprint_path = info->path;
    collapsed_node.pos = snapped_pos;
    collapsed_node.group_id = group_id;

    // Calculate size based on number of exposed ports
    size_t num_ports = std::max(info->exposed_ports.size(), size_t(1));
    float height = 80.0f + (num_ports - 1) * 16.0f;
    collapsed_node.size = Pt(120.0f, height);

    // Add exposed ports from BlueprintInfo
    for (const auto& [port_name, port] : info->exposed_ports) {
        if (port.direction == PortDirection::In) {
            collapsed_node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
        } else {
            collapsed_node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
        }
    }

    scene.blueprint().add_node(collapsed_node);

    // Create CollapsedGroup for editor-only visual collapsing
    CollapsedGroup collapsed_group;
    collapsed_group.id = unique_id;
    collapsed_group.blueprint_path = info->path;
    collapsed_group.type_name = blueprint_name;
    collapsed_group.pos = snapped_pos;
    collapsed_group.size = Pt(120.0f, height);
    collapsed_group.internal_node_ids = internal_node_ids;
    scene.blueprint().collapsed_groups.push_back(collapsed_group);

    // Recompute group_ids from collapsed_groups state
    scene.blueprint().recompute_group_ids();

    // Auto-layout the internal nodes of this group
    scene.blueprint().auto_layout_group(unique_id);

    // Clear visual cache to force rebuild
    scene.cache().clear();

    // Rebuild simulation (no expansion needed - devices are already flattened)
    rebuild_simulation();

    spdlog::info("[editor] added expanded blueprint: {} (id={}) with {} internal devices, {} internal connections",
                 blueprint_name, unique_id, internal_node_ids.size(), nested_ctx.connections.size());
}

void EditorApp::trigger_switch(const std::string& node_id) {
    // Toggle control value: 0.0 → 1.0 → 0.0 → 1.0 (Level Toggle pattern)
    float current = simulation.get_port_value(node_id, "control");
    float next = (current < 0.5f) ? 1.0f : 0.0f;

    std::string control_port = node_id + ".control";
    signal_overrides[control_port] = next;

    printf("Trigger switch: %s.control = %.1f (was %.1f)\n", node_id.c_str(), next, current);
}

void EditorApp::hold_button_press(const std::string& node_id) {
    // Add to held buttons set - control=1.0V will be sent every frame
    held_buttons.insert(node_id);
}

void EditorApp::hold_button_release(const std::string& node_id) {
    // Remove from held buttons set
    held_buttons.erase(node_id);

    // Send 2.0V release signal this frame
    std::string control_port = node_id + ".control";
    signal_overrides[control_port] = 2.0f;
}

void EditorApp::open_sub_window(const std::string& collapsed_group_id) {
    // Find the CollapsedGroup for this ID
    const CollapsedGroup* group = nullptr;
    for (const auto& g : blueprint.collapsed_groups) {
        if (g.id == collapsed_group_id) {
            group = &g;
            break;
        }
    }

    if (!group) {
        spdlog::error("[editor] Cannot open sub-window: collapsed group '{}' not found", collapsed_group_id);
        return;
    }

    // Open (or re-focus) a window for this group
    auto* win = window_manager.open(collapsed_group_id, group->type_name + " [" + collapsed_group_id + "]");

    spdlog::info("[editor] Opened sub-window for '{}' ({} internal nodes)",
                 collapsed_group_id, group->internal_node_ids.size());
    (void)win;
}

void EditorApp::update_simulation_step() {
    if (!simulation_running) return;

    // Send control=1.0V for all currently held HoldButtons
    for (const auto& node_id : held_buttons) {
        std::string control_port = node_id + ".control";
        signal_overrides[control_port] = 1.0f;
    }

    // Apply signal overrides (button clicks, etc.)
    simulation.apply_overrides(signal_overrides);

    // Run simulation step
    constexpr float dt = 0.016f;  // 60 Hz
    simulation.step(dt);

    // Clear overrides map (signals stay as set by components)
    signal_overrides.clear();
}
