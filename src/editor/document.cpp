#include "document.h"
#include "commands/commands.h"
#include "visual/scene_mutations.h"
#include "visual/persist.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "data/blueprint.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include "blueprint_v2/path/path.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>
#include <iostream>

int Document::next_id_ = 1;

Document::Document() {
    id_ = "doc_" + std::to_string(next_id_++);
}

std::string Document::title() const {
    if (!model_.current().name().empty()) {
        return model_.current().name();
    }
    if (!model_.current().display_name().empty()) {
        return model_.current().display_name();
    }
    return display_name_;
}

// ============================================================================
// Private helpers
// ============================================================================

using json = nlohmann::json;

static const char* sim_port_type_str(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any:
        default: return "Any";
    }
}

std::string Document::build_simulation_json() const {
    const bp2::Blueprint& bp = model_.current();
    json out = json::object();
    out["templates"] = json::object();

    json devices = json::array();
    std::set<std::string> emitted_ids;

    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        if (n.expandable) continue;

        std::string nid = std::string(interner_.resolve(n.id));
        if (!emitted_ids.insert(nid).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on sim export", nid);
            continue;
        }

        json device = json::object();
        device["name"] = nid;
        device["template_name"] = "";
        device["classname"] = std::string(interner_.resolve(n.type));
        if (!n.render_hint.empty()) {
            device["render_hint"] = n.render_hint;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;

        json ports = json::object();
        for (const auto& p : n.inputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "In"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        for (const auto& p : n.outputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "Out"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        device["ports"] = std::move(ports);

        json params = json::object();
        for (const auto& [k, v] : n.params) {
            params[std::string(interner_.resolve(k))] = std::to_string(v);
        }
        for (const auto& [k, v] : n.string_params) {
            params[k] = v;
        }
        if (!params.empty()) {
            device["params"] = std::move(params);
        }

        devices.push_back(std::move(device));
    }
    out["devices"] = std::move(devices);

    std::set<std::string> blueprint_node_ids;
    for (const auto& n : bp.nodes()) {
        if (n.expandable) {
            blueprint_node_ids.insert(std::string(interner_.resolve(n.id)));
        }
    }

    json connections = json::array();
    std::set<std::string> emitted_conn_keys;

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node, src_port] = bp2_path_to_node_port(w.source);
        auto [tgt_node, tgt_port] = bp2_path_to_node_port(w.target);
        if (src_node.empty() || src_port.empty() || tgt_node.empty() || tgt_port.empty())
            continue;

        std::string src_node_s = std::string(interner_.resolve(src_node));
        std::string src_port_s = std::string(interner_.resolve(src_port));
        std::string tgt_node_s = std::string(interner_.resolve(tgt_node));
        std::string tgt_port_s = std::string(interner_.resolve(tgt_port));

        if (blueprint_node_ids.count(src_node_s) > 0) {
            src_node_s = src_node_s + ":" + src_port_s;
            src_port_s = "ext";
        }
        if (blueprint_node_ids.count(tgt_node_s) > 0) {
            tgt_node_s = tgt_node_s + ":" + tgt_port_s;
            tgt_port_s = "ext";
        }

        const std::string key = src_node_s + "." + src_port_s + "→" + tgt_node_s + "." + tgt_port_s;
        if (!emitted_conn_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate connection on sim export: {}", key);
            continue;
        }

        json conn = json::object();
        conn["from"] = src_node_s + "." + src_port_s;
        conn["to"] = tgt_node_s + "." + tgt_port_s;
        connections.push_back(std::move(conn));
    }
    out["connections"] = std::move(connections);

    return out.dump(2);
}

/// Extract (node_id, port_name) from a bp2::Path (Node→Port path).
std::pair<ui::InternedId, ui::InternedId>
Document::bp2_path_to_node_port(const bp2::Path& path) const {
    if (path.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port_name = path.segment();
    bp2::Path parent = arena_.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    ui::InternedId node_id = parent.segment();
    return {node_id, port_name};
}

// ============================================================================
// File I/O
// ============================================================================

bool Document::save(const std::string& path) {
    // Sync viewport state into the bp2 blueprint before saving
    const auto& vp = viewport();
    auto updated = model_.current().with_viewport(vp.pan.x, vp.pan.y, vp.zoom, vp.grid_step);
    model_.replace_current(std::move(updated));

    TypeRegistry parser_registry = load_type_registry("library/");
    std::string validation_error;
    if (!validate_blueprint_for_persist(model_.current(), interner_, arena_, parser_registry, &validation_error)) {
        spdlog::error("[persist] Refusing to save invalid blueprint '{}': {}", path, validation_error);
        return false;
    }

    if (!save_blueprint_to_file(model_.current(), interner_, arena_, path.c_str()))
        return false;

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    model_.mark_saved();
    return true;
}

bool Document::load(const std::string& path) {
    TypeRegistry parser_registry = load_type_registry("library/");
    auto bp = load_blueprint_from_file_validated(path.c_str(), interner_, arena_, parser_registry);
    if (!bp.has_value()) return false;

    // Close any sub-windows from previous blueprint
    window_manager_.close_all();

    // Cancel any in-flight gesture in the root window before replacing the blueprint
    root().input.cancel_gesture();

    // Stop simulation if running
    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

    // Replace model with loaded blueprint and reset history
    model_.replace_current(std::move(*bp));

    // Clear undo/redo history from previous document and mark as clean
    // (push a clean checkpoint by discarding any pending undo state)
    // EditorModel: history is reset by constructing a fresh model state.
    // We achieve this by loading a fresh model:
    {
        bp2::EditorModel fresh(model_.current());
        model_ = std::move(fresh);
        sync_next_wire_id();
        model_.mark_saved();
    }

    auto& vp = viewport();
    vp.pan.x     = model_.current().pan_x();
    vp.pan.y     = model_.current().pan_y();
    vp.zoom      = model_.current().zoom();
    vp.grid_step = model_.current().grid_step();
    vp.clamp_zoom();

    // Rebuild visual widgets from new blueprint data
    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, root().group_id);

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    return true;
}

void Document::sync_next_wire_id() {
    int max_seen = -1;
    for (const auto& w : model_.current().wires()) {
        std::string_view wid = interner_.resolve(w.id);
        if (wid.size() <= 5 || wid.substr(0, 5) != "wire_") {
            continue;
        }
        int n = 0;
        bool ok = true;
        for (size_t i = 5; i < wid.size(); ++i) {
            char c = wid[i];
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            n = n * 10 + (c - '0');
        }
        if (ok && n > max_seen) {
            max_seen = n;
        }
    }
    model_.next_wire_id_ = max_seen + 1;
}

// ============================================================================
// Simulation
// ============================================================================

void Document::startSimulation() {
    if (!simulation_running_) {
        try {
            simulation_.start_from_json(build_simulation_json());
            simulation_running_ = true;
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to start simulation: {}", e.what());
            simulation_.stop();
        }
    }
}

void Document::stopSimulation() {
    simulation_.stop();
    simulation_running_ = false;
}

void Document::rebuildSimulation() {
    if (simulation_running_) {
        simulation_.stop();
        try {
            simulation_.start_from_json(build_simulation_json());
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
            simulation_running_ = false;
        }
    }
}

void Document::rebuildAllWindows() {
    // Cancel any in-flight gestures in ALL windows BEFORE rebuilding.
    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }
    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = model_.current().grid_step();
        visual::mutations::rebuild(win->scene, model_.current(),
                                   interner_, arena_, win->group_id);
    }
    rebuildSimulation();
}

// ============================================================================
// Simulation readout / node content
// ============================================================================

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

    // For each node in the bp2 blueprint, read simulation values and push to
    // visual widgets. We do NOT mutate the immutable bp2::Blueprint — all
    // simulation readout lives in the visual layer.
    for (const bp2::Blueprint::Node& n : model_.current().nodes()) {
        if (n.content_type == bp2::NodeContentType::None) continue;

        const std::string nid = n.name.empty()
            ? std::string(interner_.resolve(n.id))
            : n.name;
        const std::string type_name = std::string(interner_.resolve(n.type));

        // Build a mutable NodeContent snapshot from the bp2 node fields
        NodeContent content;
        content.type    = static_cast<NodeContentType>(n.content_type);
        content.label   = n.content_label;
        content.value   = n.content_value;
        content.min     = n.content_min;
        content.max     = n.content_max;
        content.unit    = n.content_unit;
        content.state   = n.content_state;
        content.tripped = n.content_tripped;

        // Update content values from simulation
        if (type_name == "Voltmeter") {
            content.value = simulation_.get_port_value(nid, "v_in");
            // Sync gauge range from params
            auto min_key = interner_.lookup("min");
            auto max_key = interner_.lookup("max");
            if (!min_key.empty()) {
                auto it = n.params.find(min_key);
                if (it != n.params.end()) content.min = it->second;
            }
            if (!max_key.empty()) {
                auto it = n.params.find(max_key);
                if (it != n.params.end()) content.max = it->second;
            }
        } else if (type_name == "IndicatorLight") {
            float brightness = simulation_.get_port_value(nid, "brightness");
            content.label = (brightness > 0.1f) ? "ON" : "OFF";
        } else if (type_name == "DMR400") {
            float v_gen = simulation_.get_port_value(nid, "v_gen");
            float v_bus = simulation_.get_port_value(nid, "v_bus");
            content.state = (v_gen > v_bus + 2.0f);
        } else if (type_name == "Switch") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            content.state = (state_voltage > 0.5f);
        } else if (type_name == "HoldButton") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            content.state = (state_voltage > 0.5f);
        } else if (type_name == "AZS") {
            float state_voltage = simulation_.get_port_value(nid, "state");
            content.state = (state_voltage > 0.5f);
            float tripped_voltage = simulation_.get_port_value(nid, "tripped");
            content.tripped = (tripped_voltage > 0.5f);
        } else if (type_name == "Slider") {
            auto min_key = interner_.lookup("min");
            auto max_key = interner_.lookup("max");
            if (!min_key.empty()) {
                auto it = n.params.find(min_key);
                if (it != n.params.end()) content.min = it->second;
            }
            if (!max_key.empty()) {
                auto it = n.params.find(max_key);
                if (it != n.params.end()) content.max = it->second;
            }
        }

        // Push updated content to visual widgets in all windows
        for (const auto& win : window_manager_.windows()) {
            if (n.group_id != win->group_id) continue;
            std::string_view node_sv = interner_.resolve(n.id);
            auto* widget = win->scene.find(node_sv);
            if (!widget) continue;
            auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
            if (nw) nw->updateContent(content);
        }
    }
}

void Document::resetNodeContent(const TypeRegistry& /*registry*/) {
    // In the bp2 world, node content is stored directly on bp2::Blueprint::Node
    // fields (content_type, content_value, etc.). Resetting is done by rebuilding
    // all windows, which re-creates widgets from the current blueprint state.
    rebuildAllWindows();
}

void Document::buildEnergizedWireSet(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const std::string& group_id) const {
    out.clear();
    if (!simulation_running_) return;

    const bp2::Blueprint& bp = model_.current();

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        // Filter wires to the active group by checking source node's group
        if (!group_id.empty()) {
            auto [src_node_id, src_port] = bp2_path_to_node_port(w.source);
            if (!src_node_id.empty()) {
                const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
                if (!sn || sn->group_id != group_id) continue;
            }
        }

        // Check source endpoint's port for voltage
        auto [src_node_id, src_port_id] = bp2_path_to_node_port(w.source);
        if (src_node_id.empty() || src_port_id.empty()) continue;

        std::string_view node_sv = interner_.resolve(src_node_id);
        std::string_view port_sv = interner_.resolve(src_port_id);
        std::string port_key;
        port_key.reserve(node_sv.size() + 1 + port_sv.size());
        port_key.append(node_sv);
        port_key.push_back('.');
        port_key.append(port_sv);

        if (simulation_.wire_is_energized(port_key)) {
            out.insert(interner_.resolve(w.id));
        }
    }
}

// ============================================================================
// Signal overrides
// ============================================================================

void Document::triggerSwitch(const std::string& node_id) {
    float current = simulation_.get_port_value(node_id, "control");
    float next = (current < 0.5f) ? 1.0f : 0.0f;
    signal_overrides_[node_id + ".control"] = next;
}

void Document::setSliderValue(const std::string& node_id, float value) {
    // Push slider value to simulation via signal override
    signal_overrides_[node_id + ".control"] = value;

    // Push to visual widgets in all windows
    auto node_iid = interner_.lookup(node_id);
    if (node_iid.empty()) return;

    const bp2::Blueprint::Node* n = model_.current().find_node(node_iid);
    if (!n) return;

    NodeContent content;
    content.type  = static_cast<NodeContentType>(n->content_type);
    content.value = value;
    content.min   = n->content_min;
    content.max   = n->content_max;

    for (const auto& win : window_manager_.windows()) {
        if (n->group_id != win->group_id) continue;
        auto* widget = win->scene.find(interner_.resolve(node_iid));
        if (!widget) continue;
        auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
        if (nw) nw->updateContent(content);
    }
}

void Document::holdButtonPress(const std::string& node_id) {
    held_buttons_.insert(node_id);
}

void Document::holdButtonRelease(const std::string& node_id) {
    held_buttons_.erase(node_id);
    signal_overrides_[node_id + ".control"] = 2.0f;
}

// ============================================================================
// Component addition
// ============================================================================

void Document::addComponent(const std::string& classname, Pt world_pos,
                              const std::string& group_id,
                              TypeRegistry& registry) {
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
    std::string unique_id = model_.generate_unique_node_id(classname, interner_);

    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, model_.current().grid_step());

    // Build bp2::Blueprint::Node
    bp2::Blueprint::Node node;
    node.id   = interner_.intern(unique_id);
    node.type = interner_.intern(classname);
    node.name = unique_id;
    node.x    = snapped_pos.x;
    node.y    = snapped_pos.y;
    node.group_id     = group_id;
    node.render_hint  = def->render_hint;
    node.expandable   = !def->cpp_class && !def->devices.empty();

    // Add ports
    for (const auto& [port_name, port_def] : def->ports) {
        auto pid = interner_.intern(port_name);
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(pid, PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(pid, PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            node.inputs.emplace_back(pid, PortSide::InOut, port_def.type);
            node.outputs.emplace_back(pid, PortSide::InOut, port_def.type);
        }
    }

    // Convert params (string→string → InternedId→float where parseable)
    for (const auto& [k, v] : def->params) {
        try {
            node.params[interner_.intern(k)] = std::stof(v);
        } catch (...) {
            node.string_params[k] = v;
        }
    }

    // Set node content from type definition
    {
        NodeContent nc = create_node_content_from_def(def);
        node.content_type   = static_cast<bp2::NodeContentType>(nc.type);
        node.content_label  = nc.label;
        node.content_value  = nc.value;
        node.content_min    = nc.min;
        node.content_max    = nc.max;
        node.content_unit   = nc.unit;
        node.content_state  = nc.state;
        node.content_tripped = nc.tripped;
    }

    // Execute via command system (undoable)
    model_.push_checkpoint();
    execute(model_, interner_, cmd_add_node(std::move(node)));
#ifndef NDEBUG
    {
        std::string err;
        if (!validate_blueprint_integrity(model_.current(), interner_, arena_, &err)) {
            throw std::runtime_error("bp2 integrity failure after add component: " + err);
        }
    }
#endif

    // Rebuild scene from blueprint state
    rebuildAllWindows();

    spdlog::info("[editor] Added component: {} (id={}) at ({:.1f}, {:.1f}) group={}",
           classname, unique_id, snapped_pos.x, snapped_pos.y,
           group_id.empty() ? "root" : group_id);
}

void Document::addBlueprint(const std::string& blueprint_name, Pt /*world_pos*/,
                              const std::string& /*group_id*/,
                              TypeRegistry& /*registry*/) {
    // TODO(phase-8): Port addBlueprint to bp2 command system.
    // This requires: expand_type_definition → build bp2 nodes/wires/nested,
    // then CmdAddNode * N + CmdAddNested + auto-layout.
    spdlog::warn("[editor] addBlueprint('{}') not yet implemented in bp2 mode", blueprint_name);
}

// ============================================================================
// Sub-windows
// ============================================================================

void Document::openSubWindow(const std::string& sub_blueprint_id) {
    const bp2::Blueprint::Nested* nested = nullptr;
    auto lookup_id = interner_.lookup(sub_blueprint_id);
    if (!lookup_id.empty()) {
        nested = model_.current().find_nested(lookup_id);
    }

    if (!nested) {
        spdlog::error("[editor] Cannot open sub-window: nested '{}' not found", sub_blueprint_id);
        return;
    }

    std::string type_name = std::string(interner_.resolve(nested->blueprint_id));
    auto* win = window_manager_.open(sub_blueprint_id,
                                     type_name + " [" + sub_blueprint_id + "]");
    if (win) {
        // Non-embedded (reference) sub-blueprints are read-only
        win->set_read_only(!nested->embedded);
    }

    spdlog::info("[editor] Opened sub-window for '{}'", sub_blueprint_id);
}

// ============================================================================
// Input result dispatch
// ============================================================================

Document::InputResultAction Document::applyInputResult(const InputResult& r,
                                                        const std::string& group_id) {
    InputResultAction action;

    if (r.rebuild_simulation) {
        rebuildSimulation();
        window_manager_.remove_orphaned_windows();
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
    if (!r.slider_node_id.empty()) {
        setSliderValue(r.slider_node_id, r.slider_value);
    }

    return action;
}

// ============================================================================
// Undo / Redo
// ============================================================================

bool Document::performUndo() {
    if (!model_.can_undo()) return false;

    // Cancel any in-flight gestures in ALL windows BEFORE rebuilding.
    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }

    model_.undo();
#ifndef NDEBUG
    {
        std::string err;
        if (!validate_blueprint_integrity(model_.current(), interner_, arena_, &err)) {
            throw std::runtime_error("bp2 integrity failure after undo: " + err);
        }
    }
#endif

    // Sub-blueprint groups may have been removed — close orphaned sub-windows.
    window_manager_.remove_orphaned_windows();

    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = model_.current().grid_step();
        visual::mutations::rebuild(win->scene, model_.current(),
                                   interner_, arena_, win->group_id);
    }
    rebuildSimulation();
    return true;
}

bool Document::performRedo() {
    if (!model_.can_redo()) return false;

    // Cancel any in-flight gestures in ALL windows BEFORE rebuilding.
    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }

    model_.redo();
#ifndef NDEBUG
    {
        std::string err;
        if (!validate_blueprint_integrity(model_.current(), interner_, arena_, &err)) {
            throw std::runtime_error("bp2 integrity failure after redo: " + err);
        }
    }
#endif

    // Sub-blueprint groups may have been removed — close orphaned sub-windows.
    window_manager_.remove_orphaned_windows();

    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = model_.current().grid_step();
        visual::mutations::rebuild(win->scene, model_.current(),
                                   interner_, arena_, win->group_id);
    }
    rebuildSimulation();
    return true;
}
