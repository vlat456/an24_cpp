#include "document.h"
#include "commands/commands.h"
#include "visual/scene_mutations.h"
#include "visual/persist.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>
#include <iostream>

int Document::next_id_ = 1;

Document::Document() {
    id_ = "doc_" + std::to_string(next_id_++);
}

std::string Document::title() const {
    return display_name_;
}

bool Document::save(const std::string& path) {
    // Sync viewport state into blueprint before saving
    auto& vp = viewport();
    blueprint_.pan = vp.pan;
    blueprint_.zoom = vp.zoom;
    blueprint_.grid_step = vp.grid_step;

    if (!save_blueprint_to_file(blueprint_, path.c_str())) return false;

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    undo_stack_.mark_saved();
    return true;
}

bool Document::load(const std::string& path) {
    auto bp = load_blueprint_from_file(path.c_str());
    if (!bp.has_value()) return false;

    // Close any sub-windows from previous blueprint
    window_manager_.closeAll();

    // Stop simulation if running
    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

    blueprint_ = std::move(*bp);
    blueprint_.rebuild_node_index();
    blueprint_.rebuild_wire_index();
    blueprint_.rebuild_wire_id_index();
    blueprint_.rebuild_bus_wire_index();
    blueprint_.rebuild_port_occupancy_index();

    // Clear undo/redo history from previous document and mark as clean
    undo_stack_.clear();
    undo_stack_.mark_saved();

    auto& vp = viewport();
    vp.pan = blueprint_.pan;
    vp.zoom = blueprint_.zoom;
    vp.grid_step = blueprint_.grid_step;
    vp.clamp_zoom();

    // Rebuild visual widgets from new blueprint data
    visual::mutations::rebuild(scene(), blueprint_, root().group_id);

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    return true;
}

void Document::startSimulation() {
    if (!simulation_running_) {
        try {
            simulation_.start(blueprint_);
            simulation_running_ = true;
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to start simulation: {}", e.what());
            simulation_.stop();
        }
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
        try {
            simulation_.start(blueprint_);
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
            simulation_running_ = false;
        }
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

    auto& I = blueprint_.interner();
    auto R = [&](ui::InternedId id) -> std::string { return std::string(I.resolve(id)); };

    for (auto& node : blueprint_.nodes) {
        std::string nid = R(node.id);
        // Update Voltmeter gauge voltage
        if (node.type_name == "Voltmeter") {
            float voltage = simulation_.get_port_value(nid, "v_in");
            node.node_content.value = voltage;
        }
        // Update IndicatorLight text based on brightness
        else if (node.type_name == "IndicatorLight") {
            float brightness = simulation_.get_port_value(nid, "brightness");
            node.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
        }
        // Update DMR400 state
        else if (node.type_name == "DMR400") {
            float v_gen = simulation_.get_port_value(nid, "v_gen");
            float v_bus = simulation_.get_port_value(nid, "v_bus");
            bool connected = v_gen > v_bus + 2.0f;
            node.node_content.state = connected;
        }
        // Update Switch toggle state from state port (1.0V = closed, 0.0V = open)
        else if (node.type_name == "Switch") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update HoldButton state from state port (1.0V = pressed, 0.0V = released/idle)
        else if (node.type_name == "HoldButton") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update AZS (circuit breaker) state + tripped indicator
        else if (node.type_name == "AZS") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            node.node_content.state = (state_voltage > 0.5f);
            float tripped_voltage = simulation_.get_port_value(nid, "tripped");
            node.node_content.tripped = (tripped_voltage > 0.5f);
        }
    }

    // Push updated content to visual widgets in all windows
    for (const auto& win : window_manager_.windows()) {
        for (const auto& node : blueprint_.nodes) {
            if (node.node_content.type == NodeContentType::None) continue;
            if (node.group_id != win->group_id) continue;
            auto* widget = win->scene.find(I.resolve(node.id));
            if (!widget) continue;
            auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
            if (nw) nw->updateContent(node.node_content);
        }
    }
}

void Document::resetNodeContent(const TypeRegistry& registry) {
    for (auto& node : blueprint_.nodes) {
        const auto* def = registry.get(node.type_name);
        if (!def) continue;
        node.node_content = create_node_content_from_def(def);
    }
}

void Document::buildEnergizedWireSet(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const std::string& group_id) const {
    out.clear();
    if (!simulation_running_) return;

    const auto& I = blueprint_.interner();

    for (const auto& w : blueprint_.wires) {
        // Filter wires to the active group
        // A wire belongs to a group if both endpoints are in that group
        if (!group_id.empty()) {
            const auto* start_node = blueprint_.find_node(w.start.node_id);
            if (!start_node || start_node->group_id != group_id) continue;
        }

        // Check either endpoint's port for voltage
        std::string_view node_sv = I.resolve(w.start.node_id);
        std::string_view port_sv = I.resolve(w.start.port_name);
        std::string port_key;
        port_key.reserve(node_sv.size() + 1 + port_sv.size());
        port_key.append(node_sv);
        port_key.push_back('.');
        port_key.append(port_sv);
        if (simulation_.wire_is_energized(port_key)) {
            out.insert(I.resolve(w.id));
        }
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
                              TypeRegistry& registry) {

     // Check if component exists in registry
    if (!registry.has(classname)) {
        spdlog::error("[editor] Unknown component classname '{}'", classname);
        return;
    }

    const auto* def = registry.get(classname);
    if (!def) {
        spdlog::error("[editor] Component definition not found for '{}'", classname);
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
    } while (blueprint_.find_node(unique_id.c_str()) != nullptr);

    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint_.grid_step);

    // Create node
    Node node;
    node.id = blueprint_.interner().intern(unique_id);
    node.name = unique_id;
    node.type_name = classname;
    node.pos = snapped_pos;
    node.group_id = group_id;

    node.render_hint = def->render_hint;
    node.expandable = !def->cpp_class && !def->devices.empty();

    node.size = get_default_node_size(classname, &registry);

    // Add ports
    auto& I = blueprint_.interner();
    for (const auto& [port_name, port_def] : def->ports) {
        auto pid = I.intern(port_name);
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(pid, PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(pid, PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            node.inputs.emplace_back(pid, PortSide::InOut, port_def.type);
            node.outputs.emplace_back(pid, PortSide::InOut, port_def.type);
        }
    }

    node.params = def->params;
    node.node_content = create_node_content_from_def(def);

    // Execute via command system (undoable)
    undo_stack_.snapshot(blueprint_);
    execute(blueprint_, cmd_add_node(std::move(node)));

    // Rebuild scene from blueprint state
    for (auto& win : window_manager_.windows()) {
        visual::mutations::rebuild(win->scene, blueprint_, win->group_id);
    }
    rebuildSimulation();

    spdlog::info("[editor] Added component: {} (id={}) at ({:.1f}, {:.1f}) group={}",
           classname, unique_id, snapped_pos.x, snapped_pos.y,
           group_id.empty() ? "root" : group_id);
}

void Document::addBlueprint(const std::string& blueprint_name, Pt world_pos,
                              const std::string& group_id,
                              TypeRegistry& registry) {

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
    } while (blueprint_.find_node(unique_id.c_str()) != nullptr);

    Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint_.grid_step);

    std::string category;
    auto cat_it = registry.categories.find(blueprint_name);
    if (cat_it != registry.categories.end())
        category = cat_it->second;

    Blueprint sub_bp = expand_type_definition(*bp_def, registry);
    bool has_layout = std::any_of(sub_bp.nodes.begin(), sub_bp.nodes.end(),
                                  [](const Node& n) { return n.pos.x != 0 || n.pos.y != 0; });

    auto& I = blueprint_.interner();
    auto& sub_I = sub_bp.interner();

    // == Single snapshot before all mutations (single undo entry) ==
    undo_stack_.snapshot(blueprint_);

    // 1. Add internal nodes
    std::vector<std::string> internal_node_ids;
    for (auto& node : sub_bp.nodes) {
        std::string prefixed_id = unique_id + ":" + std::string(sub_I.resolve(node.id));
        node.id = I.intern(prefixed_id);
        node.name = prefixed_id;
        if (!has_layout) node.pos = snapped_pos;
        internal_node_ids.push_back(prefixed_id);
        execute(blueprint_, cmd_add_node(std::move(node)));
    }

    // 2. Add internal wires
    size_t internal_wire_count = sub_bp.wires.size();
    for (auto& wire : sub_bp.wires) {
        wire.start.node_id = I.intern(unique_id + ":" + std::string(sub_I.resolve(wire.start.node_id)));
        wire.end.node_id = I.intern(unique_id + ":" + std::string(sub_I.resolve(wire.end.node_id)));
        wire.id = I.intern(unique_id + ":" + std::string(sub_I.resolve(wire.id)));
        execute(blueprint_, cmd_add_wire(std::move(wire)));
    }

    // 3. Create collapsed Blueprint node
    Node collapsed_node;
    collapsed_node.id = I.intern(unique_id);
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
        auto pid = I.intern(port_name);
        if (port.direction == PortDirection::In) {
            collapsed_node.inputs.emplace_back(pid, PortSide::Input, port.type);
        } else {
            collapsed_node.outputs.emplace_back(pid, PortSide::Output, port.type);
        }
    }

    execute(blueprint_, cmd_add_node(std::move(collapsed_node)));

    // 4. Register sub-blueprint instance (tracks internal nodes for undo)
    SubBlueprintInstance sbi;
    sbi.id = unique_id;
    sbi.blueprint_path = category.empty() ? blueprint_name : (category + "/" + blueprint_name);
    sbi.type_name = blueprint_name;
    sbi.pos = snapped_pos;
    sbi.size = Pt(120.0f, height);
    sbi.baked_in = false;
    sbi.internal_node_ids = internal_node_ids;
    execute(blueprint_, cmd_add_sub_blueprint(std::move(sbi)));

    blueprint_.recompute_group_ids();

    if (!has_layout) {
        blueprint_.auto_layout_group(unique_id);
    }

    // Rebuild visual + simulation
    visual::mutations::rebuild(scene(), blueprint_, root().group_id);
    rebuildSimulation();

    spdlog::info("[editor] added expanded blueprint: {} (id={}) with {} internal devices, {} internal wires",
                 blueprint_name, unique_id, internal_node_ids.size(), internal_wire_count);
}

void Document::openSubWindow(const std::string& sub_blueprint_id) {
    const SubBlueprintInstance* group = nullptr;
    for (const auto& g : blueprint_.sub_blueprint_instances) {
        if (g.id == sub_blueprint_id) {
            group = &g;
            break;
        }
    }

    if (!group) {
        spdlog::error("[editor] Cannot open sub-window: sub-blueprint '{}' not found", sub_blueprint_id);
        return;
    }

    auto* win = window_manager_.open(sub_blueprint_id, group->type_name + " [" + sub_blueprint_id + "]");
    if (win) {
        win->set_read_only(!group->baked_in);
    }

    spdlog::info("[editor] Opened sub-window for '{}' ({} internal nodes)",
                 sub_blueprint_id, group->internal_node_ids.size());
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
        action.context_menu_node_id = r.context_menu_node_id;
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

bool Document::performUndo() {
    if (!undo_stack_.can_undo()) return false;
    
    undo_stack_.undo(blueprint_);
    
    // Sub-blueprint groups may have been removed by undo — close orphaned
    // sub-windows BEFORE rebuilding, so we don't rebuild into stale groups.
    window_manager_.removeOrphanedWindows();
    
    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = blueprint_.grid_step;
        visual::mutations::rebuild(win->scene, blueprint_, win->group_id);
    }
    rebuildSimulation();
    return true;
}

bool Document::performRedo() {
    if (!undo_stack_.can_redo()) return false;
    
    undo_stack_.redo(blueprint_);
    
    // Sub-blueprint groups may have been removed by redo — close orphaned
    // sub-windows BEFORE rebuilding, so we don't rebuild into stale groups.
    window_manager_.removeOrphanedWindows();
    
    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = blueprint_.grid_step;
        visual::mutations::rebuild(win->scene, blueprint_, win->group_id);
    }
    rebuildSimulation();
    return true;
}
