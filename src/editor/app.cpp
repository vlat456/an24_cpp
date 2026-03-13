#include "app.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

void EditorApp::update_node_content_from_simulation() {
    if (!simulation_running) return;

    for (auto& node : blueprint.nodes) {
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
        // Update AZS (circuit breaker) state + tripped indicator
        else if (node.type_name == "AZS") {
            float state_voltage = simulation.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
            float tripped_voltage = simulation.get_port_value(node.id, "tripped");
            node.node_content.tripped = (tripped_voltage > 0.5f);
        }
        // Relay has no UI - automatic device controlled by external signals
    }
}

// Reset node_content to default values (used when simulation stops)
// BUGFIX [f7a3b1] Generic reset: re-derive from TypeDefinition defaults.
// Old code had a hardcoded type list (Voltmeter, IndicatorLight, DMR400, Switch, HoldButton);
// any new component type added to the simulation would NOT have its visual state reset.
void EditorApp::reset_node_content() {

    for (auto& node : blueprint.nodes) {
        const auto* def = type_registry.get(node.type_name);
        if (!def) continue;
        node.node_content = create_node_content_from_def(def);
    }
}

void EditorApp::open_properties_for_node(const std::string& node_id) {
    Node* node = blueprint.find_node(node_id.c_str());
    if (!node) return;
    properties_window.open(*node, [this](const std::string& nid) {
        inspector.markDirty();
        rebuild_simulation();
    });
}

void EditorApp::open_color_picker_for_node(const std::string& node_id) {
    const Node* node = blueprint.find_node(node_id.c_str());
    if (!node) return;
    color_picker_node_id = node_id;
    color_picker_group_id = node_context_menu_group_id;
    show_color_picker = true;

    // Pre-fill with existing custom color or theme default
    if (node->color.has_value()) {
        color_picker_rgba[0] = node->color->r;
        color_picker_rgba[1] = node->color->g;
        color_picker_rgba[2] = node->color->b;
        color_picker_rgba[3] = node->color->a;
    } else {
        // Use body fill default as starting color (COLOR_BODY_FILL = 0xFF303040)
        color_picker_rgba[0] = 0.19f;  // 0x40/255 ≈ 0.19
        color_picker_rgba[1] = 0.19f;
        color_picker_rgba[2] = 0.25f;  // 0x30/255 ≈ 0.25
        color_picker_rgba[3] = 1.0f;
    }
}

void EditorApp::add_component(const std::string& classname, Pt world_pos, const std::string& group_id) {
    
    // Check if component exists in registry
    if (!type_registry.has(classname)) {
        printf("Error: Unknown component classname '%s'\n", classname.c_str());
        return;
    }
    
    const auto* def = type_registry.get(classname);
    if (!def) {
        printf("Error: Component definition not found for '%s'\n", classname.c_str());
        return;
    }

    // Blueprint types go through add_blueprint (expanded + collapsed node)
    if (!def->cpp_class && !def->devices.empty()) {
        add_blueprint(classname, world_pos, group_id);
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
    } while (blueprint.find_node(unique_id.c_str()) != nullptr);
    
    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint.grid_step);
    
    // Create node
    Node node;
    node.id = unique_id;
    node.name = unique_id;  // Use ID as display name for now
    node.type_name = classname;
    node.pos = snapped_pos;
    node.group_id = group_id;

    // Set render_hint and expandable from TypeDefinition
    node.render_hint = def->render_hint;
    node.expandable = !def->cpp_class && !def->devices.empty();

    // Get size from component definition (single source of truth)
    node.size = get_default_node_size(classname, &type_registry);
    
    // Add ports from component definition
    for (const auto& [port_name, port_def] : def->ports) {
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            // InOut ports go to both inputs and outputs
            node.inputs.emplace_back(port_name.c_str(), PortSide::InOut, port_def.type);
            node.outputs.emplace_back(port_name.c_str(), PortSide::InOut, port_def.type);
        }
    }

    // Copy default params from component definition
    node.params = def->params;

    // BUGFIX [dc3a7f] Removed dead create_node_content wrapper, call factory directly
    node.node_content = create_node_content_from_def(def);

    // Add node to blueprint and visual scene
    visual::mutations::add_node(scene, blueprint, std::move(node), group_id);

    // Keep sub_blueprint_instances.internal_node_ids in sync
    if (!group_id.empty()) {
        for (auto& g : blueprint.sub_blueprint_instances) {
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

void EditorApp::add_blueprint(const std::string& blueprint_name, Pt world_pos, const std::string& group_id) {

    // Get blueprint definition from TypeRegistry
    const auto* bp_def = type_registry.get(blueprint_name);
    if (!bp_def || bp_def->cpp_class) {
        spdlog::error("[editor] '{}' is not a blueprint type in TypeRegistry", blueprint_name);
        return;
    }

    // Generate unique ID
    int counter = 1;
    std::string base_id = blueprint_name;
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);

    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (blueprint.find_node(unique_id.c_str()) != nullptr);

    Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint.grid_step);

    // Look up category for blueprint_path (e.g., "systems/lamp_pass_through")
    std::string category;
    auto cat_it = type_registry.categories.find(blueprint_name);
    if (cat_it != type_registry.categories.end())
        category = cat_it->second;

    // Expand TypeDefinition into a sub-Blueprint (shared code path)
    Blueprint sub_bp = expand_type_definition(*bp_def, type_registry);
    bool has_layout = std::any_of(sub_bp.nodes.begin(), sub_bp.nodes.end(),
                                  [](const Node& n) { return n.pos.x != 0 || n.pos.y != 0; });

    // Merge into parent blueprint with prefix
    std::vector<std::string> internal_node_ids;
    for (auto& node : sub_bp.nodes) {
        node.id = unique_id + ":" + node.id;
        node.name = node.id;
        if (!has_layout) node.pos = snapped_pos;
        internal_node_ids.push_back(node.id);
        blueprint.add_node(std::move(node));
    }

    for (auto& wire : sub_bp.wires) {
        wire.start.node_id = unique_id + ":" + wire.start.node_id;
        wire.end.node_id = unique_id + ":" + wire.end.node_id;
        wire.id = unique_id + ":" + wire.id;
        blueprint.add_wire(std::move(wire));
    }

    // Create COLLAPSED Blueprint node (visible in parent view)
    Node collapsed_node;
    collapsed_node.id = unique_id;
    collapsed_node.name = unique_id;
    collapsed_node.type_name = blueprint_name;
    collapsed_node.expandable = true;
    collapsed_node.collapsed = true;
    collapsed_node.pos = snapped_pos;
    collapsed_node.group_id = group_id;
    collapsed_node.blueprint_path = category.empty() ? blueprint_name : (category + "/" + blueprint_name);

    size_t num_ports = std::max(bp_def->ports.size(), size_t(1));
    float height = 80.0f + (num_ports - 1) * 16.0f;
    collapsed_node.size = Pt(120.0f, height);

    for (const auto& [port_name, port] : bp_def->ports) {
        if (port.direction == PortDirection::In) {
            collapsed_node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
        } else {
            collapsed_node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
        }
    }

    blueprint.add_node(collapsed_node);

    // Create SubBlueprintInstance for editor-only visual collapsing
    SubBlueprintInstance sbi;
    sbi.id = unique_id;
    sbi.blueprint_path = category.empty() ? blueprint_name : (category + "/" + blueprint_name);
    sbi.type_name = blueprint_name;
    sbi.pos = snapped_pos;
    sbi.size = Pt(120.0f, height);
    sbi.internal_node_ids = internal_node_ids;
    sbi.baked_in = false;
    blueprint.sub_blueprint_instances.push_back(sbi);

    blueprint.recompute_group_ids();

    if (!has_layout) {
        blueprint.auto_layout_group(unique_id);
    }

    // Rebuild visual scene from updated blueprint data
    visual::mutations::rebuild(scene, blueprint, group_id);
    rebuild_simulation();

    spdlog::info("[editor] added expanded blueprint: {} (id={}) with {} internal devices, {} internal wires",
                 blueprint_name, unique_id, internal_node_ids.size(), sub_bp.wires.size());
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

void EditorApp::open_sub_window(const std::string& sub_blueprint_id) {
    // Find the SubBlueprintInstance for this ID
    const SubBlueprintInstance* group = nullptr;
    for (const auto& g : blueprint.sub_blueprint_instances) {
        if (g.id == sub_blueprint_id) {
            group = &g;
            break;
        }
    }

    if (!group) {
        spdlog::error("[editor] Cannot open sub-window: sub-blueprint '{}' not found", sub_blueprint_id);
        return;
    }

    // Open (or re-focus) a window for this group
    auto* win = window_manager.open(sub_blueprint_id, group->type_name + " [" + sub_blueprint_id + "]");

    spdlog::info("[editor] Opened sub-window for '{}' ({} internal nodes)",
                 sub_blueprint_id, group->internal_node_ids.size());
    (void)win;
}

void EditorApp::update_simulation_step(float dt) {
    if (!simulation_running) return;

    // Send control=1.0V for all currently held HoldButtons
    for (const auto& node_id : held_buttons) {
        std::string control_port = node_id + ".control";
        signal_overrides[control_port] = 1.0f;
    }

    // Apply signal overrides (button clicks, etc.)
    simulation.apply_overrides(signal_overrides);

    // Run simulation step
    simulation.step(dt);

    // Clear overrides map (signals stay as set by components)
    signal_overrides.clear();
}
