#include "document.h"

#include "signal_key_resolver.h"
#include "core/solvers/common/signal_key.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

/// Build the simulation-level node ID: "group_id:node_id" for embedded, or "node_id" for root.
std::string make_sim_id(const std::string& node_id, const std::string& group_id) {
    return group_id.empty() ? node_id : signal_key::make_child_scope_key(group_id, node_id);
}

/// Find a node either in the root blueprint (when group_id is empty) or inside
/// the nested.inline_def for the given composite group.
const bp2::Blueprint::Node* find_node_in_scope(
    const bp2::EditorModel& model,
    ui::StringInterner& interner,
    const std::string& node_id,
    const std::string& group_id) {
    const ui::InternedId node_iid = interner.lookup(node_id);
    if (node_iid.empty()) return nullptr;

    if (group_id.empty()) {
        return model.current().find_node(node_iid);
    }
    const ui::InternedId group_iid = interner.lookup(group_id);
    const bp2::Blueprint::Nested* nested = group_iid.empty()
        ? nullptr : model.current().find_nested(group_iid);
    if (nested && nested->inline_def) {
        return nested->inline_def->find_node(node_iid);
    }
    return nullptr;
}

} // namespace

void Document::startSimulation() {
    if (!simulation_running_) {
        try {
            simulation_.start_from_json(build_simulation_json());
            simulation_running_ = true;
            for (auto& win : window_manager_.windows()) {
                win->set_simulation_mode(true);
            }
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to start simulation: {}", e.what());
            simulation_.stop();
        }
    }
}

void Document::stopSimulation() {
    simulation_.stop();
    simulation_running_ = false;
    for (auto& win : window_manager_.windows()) {
        win->set_simulation_mode(false);
    }
}

void Document::rebuildSimulation() {
    if (simulation_running_) {
        simulation_.stop();
        try {
            simulation_.start_from_json(build_simulation_json());
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
            simulation_running_ = false;
            for (auto& win : window_manager_.windows()) {
                win->set_simulation_mode(false);
            }
        }
    }
}

void Document::rebuildAllWindows() {
    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }
    for (auto& win : window_manager_.windows()) {
        win->viewport.grid_step = model_.current().grid_step();
        if (win->is_external_ref() && win->external_blueprint
            && win->external_interner && win->external_arena) {
            visual::mutations::rebuild(win->scene, *win->external_blueprint,
                                       *win->external_interner, *win->external_arena, "");
        } else if (!win->group_id.empty()) {
            // For embedded subwindows, use nested.inline_def directly
            const ui::InternedId group_iid = interner_.lookup(win->group_id);
            const bp2::Blueprint::Nested* nested = group_iid.empty() ? nullptr 
                : model_.current().find_nested(group_iid);
            
            if (nested && nested->inline_def) {
                // Sync embedded_model from authoritative nested.inline_def
                if (win->embedded_model) {
                    win->embedded_model->replace_current(*nested->inline_def);
                }
                // Rebuild from inline_def (independent of root shadow nodes)
                visual::mutations::rebuild(win->scene, *nested->inline_def,
                                           interner_, arena_, "");
            } else {
                // Fallback to root filtering (for consistency if inline_def is missing)
                visual::mutations::rebuild(win->scene, model_.current(),
                                           interner_, arena_, win->group_id);
            }
        } else {
            // Root window: filter by empty group_id
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, "");
        }
    }
    rebuildSimulation();
}

void Document::updateSimulationStep(double dt) {
    if (!simulation_running_) return;

    for (const auto& node_id : held_buttons_) {
         std::string control_port = signal_key::make_node_port_key(node_id, "control");
         signal_overrides_[control_port] = 1.0f;
     }

    simulation_.apply_overrides(signal_overrides_);
    simulation_.step(dt);
    signal_overrides_.clear();
}

void Document::updateNodeContentFromSimulation() {
    if (!simulation_running_) return;

    // Helper: given a node, type name, and simulation ID prefix, update the content
    auto update_node_content = [&](const bp2::Blueprint::Node& n,
                                   const std::string& sim_id_prefix) {
        if (n.view.content_type == bp2::NodeContentType::None) return;

        const std::string local_id = std::string(interner_.resolve(n.semantic.id));
        const std::string nid = sim_id_prefix.empty() ? local_id : signal_key::make_child_scope_key(sim_id_prefix, local_id);
        const std::string type_name = std::string(interner_.resolve(n.semantic.type));

        NodeContent content;
        content.type    = n.view.content_type;
        content.label   = n.view.content_label;
        content.value   = n.view.content_value;
        content.min     = n.view.content_min;
        content.max     = n.view.content_max;
        content.unit    = n.view.content_unit;
        content.state   = n.view.content_state;
        content.tripped = n.view.content_tripped;

        if (type_name == "Voltmeter") {
            content.value = simulation_.get_port_value(nid, "v_in");
            auto min_key = interner_.lookup("min");
            auto max_key = interner_.lookup("max");
            if (!min_key.empty()) {
                auto it = n.semantic.params.find(min_key);
                if (it != n.semantic.params.end()) content.min = it->second;
            }
            if (!max_key.empty()) {
                auto it = n.semantic.params.find(max_key);
                if (it != n.semantic.params.end()) content.max = it->second;
            }
        } else if (type_name == "IndicatorLight") {
            float brightness = simulation_.get_port_value(nid, "brightness");
            content.value = std::clamp(brightness, 0.0f, 1.0f);
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
                auto it = n.semantic.params.find(min_key);
                if (it != n.semantic.params.end()) content.min = it->second;
            }
            if (!max_key.empty()) {
                auto it = n.semantic.params.find(max_key);
                if (it != n.semantic.params.end()) content.max = it->second;
            }

            float out_val = simulation_.get_port_value(nid, "out");
            if (std::isfinite(out_val)) {
                content.value = out_val;
            } else {
                float control_val = simulation_.get_port_value(nid, "control");
                if (std::isfinite(control_val)) {
                    content.value = control_val;
                }
            }
        } else if (type_name == "KnobSwitch"
                   || type_name == "RotarySwitch1ToN"
                   || type_name == "RotarySwitchNTo1") {
            float pos_val = simulation_.get_port_value(nid, "position");
            if (std::isfinite(pos_val)) {
                content.value = pos_val;
            }
            auto pos_key = interner_.lookup("positions");
            if (!pos_key.empty()) {
                auto it = n.semantic.params.find(pos_key);
                if (it != n.semantic.params.end()) content.max = it->second;
            }
        }

        // Find the widget: for root nodes, look in root window; for nested, look in subwindow
        std::string_view node_sv = interner_.resolve(n.semantic.id);
        for (const auto& win : window_manager_.windows()) {
            if (sim_id_prefix.empty()) {
                // Root node: only update in root/matching group window
                if (n.layout.group_id != win->group_id) continue;
            } else {
                // Embedded node: only update in the subwindow for this composite
                if (win->group_id != sim_id_prefix) continue;
            }
            auto* widget = win->scene.find(node_sv);
            if (!widget) continue;
            auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
            if (nw) nw->updateContent(content);
        }
    };

    // Update root-level nodes
    for (const bp2::Blueprint::Node& n : model_.current().nodes()) {
        update_node_content(n, "");
    }

    // Update nodes inside embedded composites
    for (const bp2::Blueprint::Nested& nested : model_.current().nested()) {
        if (!nested.embedded || !nested.inline_def) continue;
        const std::string parent_id = std::string(interner_.resolve(nested.id));
        for (const bp2::Blueprint::Node& inner : nested.inline_def->nodes()) {
            update_node_content(inner, parent_id);
        }
    }
}

void Document::resetNodeContent(const TypeRegistry& /*registry*/) {
    rebuildAllWindows();
}

void Document::buildEnergizedWireSet(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const std::string& group_id) const {
    out.clear();
    if (!simulation_running_) return;

    const bp2::Blueprint& bp = model_.current();

    // For embedded subwindows, check if group_id matches a nested composite
    if (!group_id.empty()) {
        const ui::InternedId group_iid = interner_.lookup(group_id);
        const bp2::Blueprint::Nested* nested = group_iid.empty()
            ? nullptr : bp.find_nested(group_iid);
        if (nested && nested->embedded && nested->inline_def) {
            // Use inline_def wires with parent-prefixed signal keys
            for (const bp2::Blueprint::Wire& w : nested->inline_def->wires()) {
                if (w.source.kind() != bp2::PathKind::Port) continue;
                ui::InternedId src_port_iid = w.source.segment();
                bp2::Path src_parent = arena_.parent(w.source);
                if (src_parent.kind() != bp2::PathKind::Node) continue;
                ui::InternedId src_node_iid = src_parent.segment();

                // Build runtime key: "parent_id:local_node.port"
                std::string_view local_node = interner_.resolve(src_node_iid);
                std::string_view local_port = interner_.resolve(src_port_iid);
                std::string port_key = signal_key::make_child_scope_key(group_id, 
                    signal_key::make_node_port_key(local_node, local_port));

                if (simulation_.wire_is_energized(port_key)) {
                    out.insert(interner_.resolve(w.id));
                }
            }
            return;
        }
    }

    // Root-level wires
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        if (!group_id.empty()) {
            auto [src_node_id, src_port] = bp2_path_to_node_port(w.source);
            if (!src_node_id.empty()) {
                const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
                if (!sn || sn->layout.group_id != group_id) continue;
            }
        }

        auto [src_node_id, src_port_id] = bp2_path_to_node_port(w.source);
        if (src_node_id.empty() || src_port_id.empty()) continue;

        const bp2::Blueprint::Node* node = bp.find_node(src_node_id);
        editor::SignalEndpoint endpoint{node, src_node_id, src_port_id};
        editor::SignalKeyContext context = editor::root_signal_context();
        std::string port_key = editor::resolve_runtime_signal_key(bp, interner_, endpoint, context);
        if (port_key.empty()) continue;

        if (simulation_.wire_is_energized(port_key)) {
            out.insert(interner_.resolve(w.id));
        }
    }
}

void Document::buildEnergizedWireSetExternal(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const bp2::Blueprint& external_bp,
    ui::StringInterner& external_interner,
    bp2::PathArena& external_arena,
    const std::string& parent_instance_id) const {
    out.clear();
    if (!simulation_running_) return;

    for (const bp2::Blueprint::Wire& w : external_bp.wires()) {
        if (w.source.kind() != bp2::PathKind::Port) continue;
        ui::InternedId src_port_iid = w.source.segment();
        bp2::Path src_parent = external_arena.parent(w.source);
        if (src_parent.kind() != bp2::PathKind::Node) continue;
        ui::InternedId src_node_iid = src_parent.segment();

        const bp2::Blueprint::Node* node = external_bp.find_node(src_node_iid);
        editor::SignalEndpoint endpoint{node, src_node_iid, src_port_iid};
        editor::SignalKeyContext context = editor::external_ref_signal_context(parent_instance_id);
        std::string parent_key = editor::resolve_runtime_signal_key(external_bp, external_interner, endpoint, context);
        if (parent_key.empty()) continue;

        if (simulation_.wire_is_energized(parent_key)) {
            out.insert(external_interner.resolve(w.id));
        }
    }
}

void Document::triggerSwitch(const std::string& node_id, const std::string& group_id) {
     const std::string sim_id = make_sim_id(node_id, group_id);
     float current = simulation_.get_port_value(sim_id, "control");
     float next = (current < 0.5f) ? 1.0f : 0.0f;
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = next;
 }

void Document::setSliderValue(const std::string& node_id, float value, const std::string& group_id) {
     const std::string sim_id = make_sim_id(node_id, group_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = value;

    const bp2::Blueprint::Node* n = find_node_in_scope(model_, interner_, node_id, group_id);
    if (!n) return;

    NodeContent content;
    content.type  = n->view.content_type;
    content.value = value;
    content.min   = n->view.content_min;
    content.max   = n->view.content_max;

    const ui::InternedId node_iid = interner_.lookup(node_id);
    for (const auto& win : window_manager_.windows()) {
        if (group_id.empty()) {
            if (n->layout.group_id != win->group_id) continue;
        } else {
            if (win->group_id != group_id) continue;
        }
        auto* widget = win->scene.find(interner_.resolve(node_iid));
        if (!widget) continue;
        auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
        if (nw) nw->updateContent(content);
    }
}

void Document::setKnobPosition(const std::string& node_id, int position, const std::string& group_id) {
     const std::string sim_id = make_sim_id(node_id, group_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = static_cast<float>(position);

    const bp2::Blueprint::Node* n = find_node_in_scope(model_, interner_, node_id, group_id);
    if (!n) return;

    NodeContent content;
    content.type  = n->view.content_type;
    content.value = static_cast<float>(position);
    content.max   = n->view.content_max;
    content.min   = n->view.content_min;

    const ui::InternedId node_iid = interner_.lookup(node_id);
    for (const auto& win : window_manager_.windows()) {
        if (group_id.empty()) {
            if (n->layout.group_id != win->group_id) continue;
        } else {
            if (win->group_id != group_id) continue;
        }
        auto* widget = win->scene.find(interner_.resolve(node_iid));
        if (!widget) continue;
        auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
        if (nw) nw->updateContent(content);
    }
}

void Document::holdButtonPress(const std::string& node_id, const std::string& group_id) {
    held_buttons_.insert(make_sim_id(node_id, group_id));
}

void Document::holdButtonRelease(const std::string& node_id, const std::string& group_id) {
     const std::string sim_id = make_sim_id(node_id, group_id);
     held_buttons_.erase(sim_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = 2.0f;
 }
