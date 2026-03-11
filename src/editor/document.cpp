#include "document.h"
#include "visual/node/node.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "visual/node/layout.h"
#include "visual/scene/scene.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>

int Document::next_id_ = 1;

Document::Document() {
    id_ = "doc_" + std::to_string(next_id_++);
}

std::string Document::title() const {
    return display_name_ + (modified_ ? "*" : "");
}

bool Document::save(const std::string& path) {
    // Sync viewport state into blueprint before saving
    auto& vp = scene().viewport();
    blueprint_.pan = vp.pan;
    blueprint_.zoom = vp.zoom;
    blueprint_.grid_step = vp.grid_step;

    if (!save_blueprint_to_file(blueprint_, path.c_str())) return false;

    filepath_ = path;
    // Extract filename for display
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    clearModified();
    return true;
}

bool Document::load(const std::string& path) {
    auto bp = load_blueprint_from_file(path.c_str());
    if (!bp.has_value()) return false;

    blueprint_ = std::move(*bp);
    blueprint_.rebuild_wire_index();

    auto& vp = scene().viewport();
    vp.pan = blueprint_.pan;
    vp.zoom = blueprint_.zoom;
    vp.grid_step = blueprint_.grid_step;
    vp.clamp_zoom();

    scene().clearCache();
    scene().invalidateSpatialGrid();

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    clearModified();
    return true;
}

void Document::startSimulation() {
    if (!simulation_running_) {
        simulation_.start(scene().blueprint());
        simulation_running_ = true;
    }
}

void Document::stopSimulation() {
    simulation_.stop();
    // Reset visual content will be done by caller with TypeRegistry
    simulation_running_ = false;
}

void Document::rebuildSimulation() {
    if (simulation_running_) {
        simulation_.stop();
        simulation_.start(scene().blueprint());
    }
}

void Document::updateSimulationStep(float dt) {
    if (!simulation_running_) return;

    // Send control=1.0V for all currently held HoldButtons
    for (const auto& node_id : held_buttons_) {
        std::string control_port = node_id + ".control";
        signal_overrides_[control_port] = 1.0f;
    }

    // Apply signal overrides (button clicks, etc.)
    simulation_.apply_overrides(signal_overrides_);

    // Run simulation step
    simulation_.step(dt);

    // Clear overrides map (signals stay as set by components)
    signal_overrides_.clear();
}

void Document::updateNodeContentFromSimulation() {
    if (!simulation_running_) return;

    for (auto& node : scene().nodes()) {
        // Update Voltmeter gauge voltage
        if (node.type_name == "Voltmeter") {
            float voltage = simulation_.get_port_value(node.id, "v_in");
            node.node_content.value = voltage;
        }
        // Update IndicatorLight text based on brightness
        else if (node.type_name == "IndicatorLight") {
            float brightness = simulation_.get_port_value(node.id, "brightness");
            node.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
        }
        // Update DMR400 state
        else if (node.type_name == "DMR400") {
            float v_gen = simulation_.get_port_value(node.id, "v_gen");
            float v_bus = simulation_.get_port_value(node.id, "v_bus");
            bool connected = v_gen > v_bus + 2.0f;
            node.node_content.state = connected;
        }
        // Update Switch toggle state from state port (1.0V = closed, 0.0V = open)
        else if (node.type_name == "Switch") {
            float state_voltage = simulation_.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update HoldButton state from state port (1.0V = pressed, 0.0V = released/idle)
        else if (node.type_name == "HoldButton") {
            float state_voltage = simulation_.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update AZS (circuit breaker) state + tripped indicator
        else if (node.type_name == "AZS") {
            float state_voltage = simulation_.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
            float tripped_voltage = simulation_.get_port_value(node.id, "tripped");
            node.node_content.tripped = (tripped_voltage > 0.5f);
        }
    }
}

void Document::resetNodeContent(const an24::TypeRegistry& registry) {
    for (auto& node : scene().nodes()) {
        const auto* def = registry.get(node.type_name);
        if (!def) continue;
        node.node_content = create_node_content_from_def(def);
    }
}

void Document::triggerSwitch(const std::string& node_id) {
    float current = simulation_.get_port_value(node_id, "control");
    float next = (current < 0.5f) ? 1.0f : 0.0f;
    std::string control_port = node_id + ".control";
    signal_overrides_[control_port] = next;
}

void Document::holdButtonPress(const std::string& node_id) {
    held_buttons_.insert(node_id);
}

void Document::holdButtonRelease(const std::string& node_id) {
    held_buttons_.erase(node_id);
    std::string control_port = node_id + ".control";
    signal_overrides_[control_port] = 2.0f;
}

void Document::addComponent(const std::string& classname, Pt world_pos,
                             const std::string& group_id,
                             an24::TypeRegistry& registry) {
    using namespace an24;

    // Check if component exists in registry
    if (!registry.has(classname)) {
        printf("Error: Unknown component classname '%s'\n", classname.c_str());
        return;
    }

    const auto* def = registry.get(classname);
    if (!def) {
        printf("Error: Component definition not found for '%s'\n", classname.c_str());
        return;
    }

    // Blueprint types go through addBlueprint (expanded + collapsed node)
    if (!def->cpp_class && !def->devices.empty()) {
        addBlueprint(classname, world_pos, group_id, registry);
        return;
    }

    // Generate unique ID
    int counter = 1;
    std::string base_id = classname;
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);

    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (scene().findNode(unique_id.c_str()) != nullptr);

    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, scene().gridStep());

    // Create node
    Node node;
    node.id = unique_id;
    node.name = unique_id;
    node.type_name = classname;
    node.pos = snapped_pos;
    node.group_id = group_id;

    node.render_hint = def->render_hint;
    node.expandable = !def->cpp_class && !def->devices.empty();

    node.size = get_default_node_size(classname, &registry);

    // Add ports
    for (const auto& [port_name, port_def] : def->ports) {
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            node.inputs.emplace_back(port_name.c_str(), PortSide::InOut, port_def.type);
            node.outputs.emplace_back(port_name.c_str(), PortSide::InOut, port_def.type);
        }
    }

    node.params = def->params;
    node.node_content = create_node_content_from_def(def);

    scene().addNode(node);

    // Keep collapsed_groups.internal_node_ids in sync
    if (!group_id.empty()) {
        for (auto& g : blueprint_.collapsed_groups) {
            if (g.id == group_id) {
                g.internal_node_ids.push_back(unique_id);
                break;
            }
        }
    }

    rebuildSimulation();
    markModified();

    printf("Added component: %s (id=%s) at (%.1f, %.1f) group=%s\n",
           classname.c_str(), unique_id.c_str(), snapped_pos.x, snapped_pos.y,
           group_id.empty() ? "root" : group_id.c_str());
}

void Document::addBlueprint(const std::string& blueprint_name, Pt world_pos,
                             const std::string& group_id,
                             an24::TypeRegistry& registry) {
    using namespace an24;

    const auto* bp_def = registry.get(blueprint_name);
    if (!bp_def || bp_def->cpp_class) {
        spdlog::error("[editor] '{}' is not a blueprint type in TypeRegistry", blueprint_name);
        return;
    }

    int counter = 1;
    std::string base_id = blueprint_name;
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);

    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (scene().findNode(unique_id.c_str()) != nullptr);

    Pt snapped_pos = editor_math::snap_to_grid(world_pos, scene().gridStep());

    Blueprint sub_bp = expand_type_definition(*bp_def, registry);
    bool has_layout = std::any_of(sub_bp.nodes.begin(), sub_bp.nodes.end(),
                                  [](const Node& n) { return n.pos.x != 0 || n.pos.y != 0; });

    std::vector<std::string> internal_node_ids;
    for (auto& node : sub_bp.nodes) {
        node.id = unique_id + ":" + node.id;
        node.name = node.id;
        if (!has_layout) node.pos = snapped_pos;
        internal_node_ids.push_back(node.id);
        scene().blueprint().add_node(std::move(node));
    }

    for (auto& wire : sub_bp.wires) {
        wire.start.node_id = unique_id + ":" + wire.start.node_id;
        wire.end.node_id = unique_id + ":" + wire.end.node_id;
        wire.id = unique_id + ":" + wire.id;
        scene().blueprint().add_wire(std::move(wire));
    }

    // Create COLLAPSED Blueprint node
    Node collapsed_node;
    collapsed_node.id = unique_id;
    collapsed_node.name = unique_id;
    collapsed_node.type_name = blueprint_name;
    collapsed_node.expandable = true;
    collapsed_node.collapsed = true;
    collapsed_node.pos = snapped_pos;
    collapsed_node.group_id = group_id;

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

    scene().blueprint().add_node(collapsed_node);

    CollapsedGroup collapsed_group;
    collapsed_group.id = unique_id;
    collapsed_group.type_name = blueprint_name;
    collapsed_group.pos = snapped_pos;
    collapsed_group.size = Pt(120.0f, height);
    collapsed_group.internal_node_ids = internal_node_ids;
    scene().blueprint().collapsed_groups.push_back(collapsed_group);

    scene().blueprint().recompute_group_ids();

    if (!has_layout) {
        scene().blueprint().auto_layout_group(unique_id);
    }

    scene().cache().clear();
    rebuildSimulation();
    markModified();

    spdlog::info("[editor] added expanded blueprint: {} (id={}) with {} internal devices, {} internal wires",
                 blueprint_name, unique_id, internal_node_ids.size(), sub_bp.wires.size());
}

void Document::openSubWindow(const std::string& collapsed_group_id) {
    const CollapsedGroup* group = nullptr;
    for (const auto& g : blueprint_.collapsed_groups) {
        if (g.id == collapsed_group_id) {
            group = &g;
            break;
        }
    }

    if (!group) {
        spdlog::error("[editor] Cannot open sub-window: collapsed group '{}' not found", collapsed_group_id);
        return;
    }

    auto* win = window_manager_.open(collapsed_group_id, group->type_name + " [" + collapsed_group_id + "]");

    spdlog::info("[editor] Opened sub-window for '{}' ({} internal nodes)",
                 collapsed_group_id, group->internal_node_ids.size());
    (void)win;
}

Document::InputResultAction Document::applyInputResult(const InputResult& r, const std::string& group_id) {
    InputResultAction action;

    if (r.rebuild_simulation) {
        rebuildSimulation();
        window_manager_.removeOrphanedWindows();
    }
    if (r.show_context_menu) {
        action.show_context_menu = true;
        action.context_menu_pos = r.context_menu_pos;
        action.context_menu_group_id = group_id;
    }
    if (r.show_node_context_menu) {
        action.show_node_context_menu = true;
        action.context_menu_node_index = r.context_menu_node_index;
        action.node_context_menu_group_id = group_id;
    }
    if (!r.open_sub_window.empty()) {
        openSubWindow(r.open_sub_window);
    }
    if (!r.toggle_switch_node_id.empty()) {
        triggerSwitch(r.toggle_switch_node_id);
    }

    return action;
}
