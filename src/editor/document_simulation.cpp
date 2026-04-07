#include "document.h"

#include "signal_key_resolver.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

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
        } else {
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, win->group_id);
        }
    }
    rebuildSimulation();
}

void Document::updateSimulationStep(double dt) {
    if (!simulation_running_) return;

    for (const auto& node_id : held_buttons_) {
        std::string control_port = node_id + ".control";
        signal_overrides_[control_port] = 1.0f;
    }

    simulation_.apply_overrides(signal_overrides_);
    simulation_.step(dt);
    signal_overrides_.clear();
}

void Document::updateNodeContentFromSimulation() {
    if (!simulation_running_) return;

    for (const bp2::Blueprint::Node& n : model_.current().nodes()) {
        if (n.content_type == bp2::NodeContentType::None) continue;

        const std::string nid = std::string(interner_.resolve(n.id));
        const std::string type_name = std::string(interner_.resolve(n.type));

        NodeContent content;
        content.type    = n.content_type;
        content.label   = n.content_label;
        content.value   = n.content_value;
        content.min     = n.content_min;
        content.max     = n.content_max;
        content.unit    = n.content_unit;
        content.state   = n.content_state;
        content.tripped = n.content_tripped;

        if (type_name == "Voltmeter") {
            content.value = simulation_.get_port_value(nid, "v_in");
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
                auto it = n.params.find(min_key);
                if (it != n.params.end()) content.min = it->second;
            }
            if (!max_key.empty()) {
                auto it = n.params.find(max_key);
                if (it != n.params.end()) content.max = it->second;
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
                auto it = n.params.find(pos_key);
                if (it != n.params.end()) content.max = it->second;
            }
        }

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
    rebuildAllWindows();
}

void Document::buildEnergizedWireSet(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const std::string& group_id) const {
    out.clear();
    if (!simulation_running_) return;

    const bp2::Blueprint& bp = model_.current();

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        if (!group_id.empty()) {
            auto [src_node_id, src_port] = bp2_path_to_node_port(w.source);
            if (!src_node_id.empty()) {
                const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
                if (!sn || sn->group_id != group_id) continue;
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

void Document::triggerSwitch(const std::string& node_id) {
    float current = simulation_.get_port_value(node_id, "control");
    float next = (current < 0.5f) ? 1.0f : 0.0f;
    signal_overrides_[node_id + ".control"] = next;
}

void Document::setSliderValue(const std::string& node_id, float value) {
    signal_overrides_[node_id + ".control"] = value;

    auto node_iid = interner_.lookup(node_id);
    if (node_iid.empty()) return;

    const bp2::Blueprint::Node* n = model_.current().find_node(node_iid);
    if (!n) return;

    NodeContent content;
    content.type  = n->content_type;
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

void Document::setKnobPosition(const std::string& node_id, int position) {
    signal_overrides_[node_id + ".control"] = static_cast<float>(position);

    auto node_iid = interner_.lookup(node_id);
    if (node_iid.empty()) return;

    const bp2::Blueprint::Node* n = model_.current().find_node(node_iid);
    if (!n) return;

    NodeContent content;
    content.type  = n->content_type;
    content.value = static_cast<float>(position);
    content.max   = n->content_max;
    content.min   = n->content_min;

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
