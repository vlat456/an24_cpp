#include "document.h"

#include "signal_key_resolver.h"
#include "core/solvers/common/signal_key.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include "identity.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

/// Build the simulation-level node ID: "scope_id:node_id" for embedded, or "node_id" for root.
std::string make_sim_id(const editor::NodeId& node_id, const std::string& scope_id) {
    return scope_id.empty() ? node_id.str() : signal_key::make_child_scope_key(scope_id, node_id.str());
}

/// Find a node either in the root blueprint (when scope_id is empty) or inside
/// the embedded blueprint of the given blueprint-instance node.
const bp2::Blueprint::Node* find_node_in_scope(
    const bp2::EditorModel& model,
    ui::StringInterner& interner,
    const editor::NodeId& node_id,
    const std::string& scope_id) {
    const ui::InternedId node_iid = interner.lookup(node_id.str());
    if (node_iid.empty()) return nullptr;

    if (scope_id.empty()) {
        return model.current().find_node(node_iid);
    }
    const ui::InternedId group_iid = interner.lookup(scope_id);
    const bp2::Blueprint::Node* group_node = group_iid.empty()
        ? nullptr : model.current().find_node(group_iid);
    if (!group_node || !group_node->has_embedded_blueprint()) return nullptr;
    if (auto* def = group_node->source->inline_def()) {
        return def->find_node(node_iid);
    }
    return nullptr;
}

std::pair<const bp2::Blueprint::Wire*, std::string_view> find_wire_in_scope(
    const Document::ResolvedSignalScope& resolved,
    std::string_view wire_id) {
    if (!resolved.blueprint || !resolved.interner) {
        return {nullptr, {}};
    }

    const ui::InternedId wire_iid = resolved.interner->lookup(wire_id);
    if (wire_iid.empty()) {
        return {nullptr, {}};
    }

    return {resolved.blueprint->find_wire(wire_iid), resolved.interner->resolve(wire_iid)};
}

NodeContent resolve_base_content(const bp2::Blueprint::Node& node,
                                 ui::StringInterner& interner,
                                 const TypeRegistry* registry) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const TypeDefinition* def = registry ? registry->get(type_name) : nullptr;
    return create_node_content(def, node.semantic.params, node.semantic.string_params, interner);
}

void dispatch_content_to_widget(WindowManager& window_manager,
                                ui::StringInterner& interner,
                                ui::InternedId node_iid,
                                const std::string& scope_id,
                                const NodeContent& content) {
    std::string_view node_sv = interner.resolve(node_iid);
    for (const auto& win : window_manager.windows()) {
        const std::string scope_key = win->resolved_scope_id().key();
        if (scope_id.empty()) {
            if (!scope_key.empty()) continue;
        } else {
            if (scope_key != scope_id) continue;
        }
        auto* widget = win->scene.find(node_sv);
        if (!widget) continue;
        auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
        if (nw) nw->updateContent(content);
    }
}

void overlay_simulation_values(NodeContent& content,
                               const std::string& type_name,
                               const std::string& sim_node_id,
                               Simulator<JIT_Solver>& simulation) {
    if (type_name == "Voltmeter") {
        content.value = simulation.get_port_value(sim_node_id, "v_in");
    } else if (type_name == "IndicatorLight") {
        float brightness = simulation.get_port_value(sim_node_id, "brightness");
        content.value = std::clamp(brightness, 0.0f, 1.0f);
    } else if (type_name == "Switch") {
        float state_voltage = simulation.get_port_value(sim_node_id, "state");
        content.state = (state_voltage > 0.5f);
    } else if (type_name == "HoldButton") {
        float state_voltage = simulation.get_port_value(sim_node_id, "state");
        content.state = (state_voltage > 0.5f);
    } else if (type_name == "AZS") {
        float state_voltage = simulation.get_port_value(sim_node_id, "state");
        content.state = (state_voltage > 0.5f);
        float tripped_voltage = simulation.get_port_value(sim_node_id, "tripped");
        content.tripped = (tripped_voltage > 0.5f);
    } else if (type_name == "Slider") {
        float out_val = simulation.get_port_value(sim_node_id, "out");
        if (std::isfinite(out_val)) {
            content.value = out_val;
        } else {
            float control_val = simulation.get_port_value(sim_node_id, "control");
            if (std::isfinite(control_val)) {
                content.value = control_val;
            }
        }
    } else if (type_name == "KnobSwitch"
               || type_name == "RotarySwitch1ToN"
               || type_name == "RotarySwitchNTo1") {
        float pos_val = simulation.get_port_value(sim_node_id, "position");
        if (std::isfinite(pos_val)) {
            content.value = pos_val;
        }
    }
}

} // namespace

Document::ResolvedSignalScope Document::resolve_signal_scope(const WindowScopeId& scope_id) const {
    if (scope_id.is_external()) {
        if (const BlueprintWindow* win = window_manager_.find(scope_id)) {
            if (win->external_blueprint && win->external_interner) {
                return {
                    &*win->external_blueprint,
                    win->external_interner.get(),
                    editor::external_ref_signal_context(scope_id.key())
                };
            }
        }
        return {nullptr, nullptr, editor::external_ref_signal_context(scope_id.key())};
    }

    if (scope_id.is_embedded()) {
        const ui::InternedId group_iid = interner_.lookup(scope_id.key());
        const bp2::Blueprint::Node* node = group_iid.empty() ? nullptr : model_.current().find_node(group_iid);
        if (node && node->has_embedded_blueprint() && node->source->inline_def()) {
            return {
                node->source->inline_def(),
                &interner_,
                editor::embedded_signal_context(scope_id.key())
            };
        }
        return {nullptr, nullptr, editor::embedded_signal_context(scope_id.key())};
    }

    return {&model_.current(), &interner_, editor::root_signal_context()};
}

void Document::rebuild_window_scenes() {
    TypeRegistry empty_reg;
    const TypeRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    for (auto& win : window_manager_.windows()) {
        if (win->is_external_ref() && win->external_blueprint
            && win->external_interner && win->external_arena) {
            visual::mutations::rebuild(win->scene, *win->external_blueprint,
                                       *win->external_interner, *win->external_arena, "", reg);
            win->input.rebuild_snapshot();
        } else if (win->resolved_scope_id().is_embedded()) {
            const std::string scope_key = win->resolved_scope_id().key();
            const ui::InternedId group_iid = interner_.lookup(scope_key);
            const bp2::Blueprint::Node* node = group_iid.empty()
                ? nullptr
                : model_.current().find_node(group_iid);

            if (node && node->has_embedded_blueprint() && node->source->inline_def()) {
                visual::mutations::rebuild(win->scene, *node->source->inline_def(),
                                           interner_, arena_, "", reg);
                win->input.rebuild_snapshot();
            } else {
                spdlog::error("[editor] Embedded window '{}' missing embedded blueprint during rebuild", scope_key);
                continue;
            }
        } else {
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, "", reg);
            win->input.rebuild_snapshot();
        }
    }
}

void Document::startSimulation() {
    if (!simulation_running_) {
        try {
            simulation_.start(build_jit_input());
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
            simulation_.start(build_jit_input());
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
    rebuild_window_scenes();
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
        const std::string local_id = std::string(interner_.resolve(n.semantic.id));
        const std::string nid = sim_id_prefix.empty() ? local_id : signal_key::make_child_scope_key(sim_id_prefix, local_id);
        const std::string type_name = std::string(interner_.resolve(n.semantic.type));
        NodeContent content = resolve_base_content(n, interner_, type_registry_);
        if (content.type == bp2::NodeContentType::None) return;

        overlay_simulation_values(content, type_name, nid, simulation_);
        dispatch_content_to_widget(window_manager_, interner_, n.semantic.id, sim_id_prefix, content);
    };

    // Update root-level nodes
    for (const bp2::Blueprint::Node& n : model_.current().nodes()) {
        update_node_content(n, "");
    }

    // Update nodes inside embedded blueprint instances
    for (const bp2::Blueprint::Node& parent_node : model_.current().nodes()) {
        if (!parent_node.has_embedded_blueprint() || !parent_node.source->inline_def()) continue;
        const std::string parent_id = std::string(interner_.resolve(parent_node.semantic.id));
        for (const bp2::Blueprint::Node& inner : parent_node.source->inline_def()->nodes()) {
            update_node_content(inner, parent_id);
        }
    }
}

void Document::resetNodeContent(const TypeRegistry& /*registry*/) {
    rebuildAllWindows();
}

void Document::buildEnergizedWireSet(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const WindowScopeId& scope_id) const {
    out.clear();
    if (!simulation_running_) return;

    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    if (!resolved.blueprint || !resolved.interner) {
        return;
    }

    for (const bp2::Blueprint::Wire& w : resolved.blueprint->wires()) {
        auto [src_node_id, src_port_id] = bp2_path_to_node_port(w.source);
        if (src_node_id.empty() || src_port_id.empty()) continue;

        const bp2::Blueprint::Node* node = resolved.blueprint->find_node(src_node_id);
        editor::SignalEndpoint endpoint{node, src_node_id, src_port_id};
        std::string port_key = editor::resolve_runtime_signal_key(
            *resolved.blueprint, *resolved.interner, endpoint, resolved.context);
        if (port_key.empty()) continue;

        if (simulation_.wire_is_energized(port_key)) {
            out.insert(resolved.interner->resolve(w.id));
        }
    }
}

std::string Document::resolve_endpoint_signal_key(const WindowScopeId& scope_id,
                                                  std::string_view node_id,
                                                  std::string_view port_name) const {
    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    if (!resolved.blueprint || !resolved.interner) {
        return "";
    }

    const ui::InternedId node_iid = resolved.interner->lookup(node_id);
    const ui::InternedId port_iid = resolved.interner->lookup(port_name);
    if (node_iid.empty() || port_iid.empty()) {
        return "";
    }

    const bp2::Blueprint::Node* node = resolved.blueprint->find_node(node_iid);
    const editor::SignalEndpoint endpoint{node, node_iid, port_iid};
    return editor::resolve_runtime_signal_key(
        *resolved.blueprint, *resolved.interner, endpoint, resolved.context);
}

std::string Document::resolve_wire_signal_key(const WindowScopeId& scope_id,
                                              std::string_view wire_id) const {
    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    const auto [wire, resolved_wire_id] = find_wire_in_scope(resolved, wire_id);
    if (!wire || resolved_wire_id.empty()) {
        return "";
    }

    if (wire->source.node.empty() || wire->source.port.empty()) {
        return "";
    }

    const bp2::Blueprint::Node* node = resolved.blueprint->find_node(wire->source.node);
    const editor::SignalEndpoint endpoint{node, wire->source.node, wire->source.port};
    return editor::resolve_runtime_signal_key(
        *resolved.blueprint, *resolved.interner, endpoint, resolved.context);
}

void Document::triggerSwitch(const editor::NodeId& node_id, const std::string& scope_id) {
     const std::string sim_id = make_sim_id(node_id, scope_id);
     float current = simulation_.get_port_value(sim_id, "control");
     float next = (current < 0.5f) ? 1.0f : 0.0f;
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = next;
 }

void Document::setSliderValue(const editor::NodeId& node_id, float value, const std::string& scope_id) {
     const std::string sim_id = make_sim_id(node_id, scope_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = value;

    const bp2::Blueprint::Node* n = find_node_in_scope(model_, interner_, node_id, scope_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = value;
    dispatch_content_to_widget(window_manager_, interner_, n->semantic.id, scope_id, content);
}

void Document::setKnobPosition(const editor::NodeId& node_id, int position, const std::string& scope_id) {
     const std::string sim_id = make_sim_id(node_id, scope_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = static_cast<float>(position);

    const bp2::Blueprint::Node* n = find_node_in_scope(model_, interner_, node_id, scope_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = static_cast<float>(position);
    dispatch_content_to_widget(window_manager_, interner_, n->semantic.id, scope_id, content);
}

void Document::holdButtonPress(const editor::NodeId& node_id, const std::string& scope_id) {
    held_buttons_.insert(make_sim_id(node_id, scope_id));
}

void Document::holdButtonRelease(const editor::NodeId& node_id, const std::string& scope_id) {
     const std::string sim_id = make_sim_id(node_id, scope_id);
     held_buttons_.erase(sim_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = 2.0f;
 }
