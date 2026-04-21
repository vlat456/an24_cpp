#include "document.h"
#include "document_simulation_internal.h"

#include "signal_key_resolver.h"
#include "core/solvers/common/signal_key.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include "identity.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <spdlog/spdlog.h>

namespace {

/// Build the simulation-level node ID: "scope_id:node_id" for embedded, or "node_id" for root.
std::string make_sim_id(const editor::NodeId& node_id, const std::string& scope_id) {
    return scope_id.empty() ? node_id.str() : signal_key::make_child_scope_key(scope_id, node_id.str());
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
                                   const ComponentRegistry* registry) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const auto* def = registry ? registry->get(type_name) : nullptr;
    const TypePresentation* pres = registry ? registry->presentation.get(type_name) : nullptr;
    if (!def) return NodeContent{};
    return create_node_content(*def, pres, node.semantic.params, node.semantic.string_params, interner);
}

editor::RuntimeNodeState default_runtime_state(const bp2::NodeContentType content_type,
                                               const NodeContent& content) {
    switch (content_type) {
        case bp2::NodeContentType::Slider:
        case bp2::NodeContentType::Gauge:
        case bp2::NodeContentType::Indicator:
            return editor::ScalarNodeRuntimeState{content.value};
        case bp2::NodeContentType::Knob:
            return editor::DiscreteNodeRuntimeState{static_cast<int>(content.value)};
        case bp2::NodeContentType::Switch:
        case bp2::NodeContentType::VerticalToggle:
            return editor::BoolNodeRuntimeState{content.state};
        case bp2::NodeContentType::Text:
        case bp2::NodeContentType::None:
        case bp2::NodeContentType::Value:
        case bp2::NodeContentType::Count:
            return std::monostate{};
    }
    return std::monostate{};
}

editor::RuntimeNodeState build_runtime_state(const bp2::Blueprint::Node& node,
                                              ui::StringInterner& interner,
                                              const ComponentRegistry* registry) {
    const NodeContent content = resolve_base_content(node, interner, registry);
    return default_runtime_state(content.type, content);
}

void dispatch_color_to_widget(WindowManager& window_manager,
                              ui::StringInterner& interner,
                              ui::InternedId node_iid,
                              const std::string& scope_id,
                              std::optional<editor::NodeColor> color) {
    std::string_view node_sv = interner.resolve(node_iid);
    for (const auto& win : window_manager.windows()) {
        const std::string scope_key = win->resolved_scope_id().key();
        if (scope_id.empty()) {
            if (!scope_key.empty()) continue;
        } else {
            if (scope_key != scope_id) continue;
        }
        if (auto* widget = win->scene.find(node_sv)) {
            widget->setCustomColor(color.has_value() ? std::optional<uint32_t>(color->to_uint32()) : std::nullopt);
        }
    }
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
                               const bp2::Blueprint::Node& node,
                               ui::StringInterner& interner,
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
        if (auto port = editor::select_slider_readback_port(node, interner)) {
            float val = simulation.get_port_value(sim_node_id, std::string(*port));
            if (std::isfinite(val)) {
                content.value = val;
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

namespace editor {

std::optional<std::string_view> select_slider_readback_port(const bp2::Blueprint::Node& node,
                                                             ui::StringInterner& interner) {
    if (node.component().iface.has(interner.intern("out"))) {
        return std::string_view{"out"};
    }
    if (node.component().iface.has(interner.intern("control"))) {
        return std::string_view{"control"};
    }
    return std::nullopt;
}

void walk_blueprint_nodes(const bp2::Blueprint& bp,
                          std::vector<ui::InternedId>& instance_path,
                          const std::function<void(const bp2::Blueprint::Node&, std::span<const ui::InternedId>)>& fn) {
    for (const bp2::Blueprint::Node& node : bp.nodes()) {
        fn(node, instance_path);
        if (!node.has_embedded_blueprint() || !node.blueprint_instance().source.inline_def()) {
            continue;
        }
        instance_path.push_back(node.semantic.id);
        walk_blueprint_nodes(*node.blueprint_instance().source.inline_def(), instance_path, fn);
        instance_path.pop_back();
    }
}

} // namespace editor

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
        if (node && node->has_embedded_blueprint() && node->blueprint_instance().source.inline_def()) {
            return {
                node->blueprint_instance().source.inline_def(),
                &interner_,
                editor::embedded_signal_context(scope_id.key())
            };
        }
        return {nullptr, nullptr, editor::embedded_signal_context(scope_id.key())};
    }

    return {&model_.current(), &interner_, editor::root_signal_context()};
}

void Document::rebuild_window_scenes() {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    for (auto& win : window_manager_.windows()) {
        const std::string scope_key = win->resolved_scope_id().sim_scope_prefix();

        if (win->is_external_ref() && win->external_blueprint
            && win->external_interner && win->external_arena) {
            // External windows use the parent instance id as scope key —
            // must match the scope used when the window was first opened.
            visual::mutations::rebuild(win->scene, *win->external_blueprint,
                                       *win->external_interner, *win->external_arena, scope_key, reg,
                                       &runtime_node_states_, &session_node_appearance_);
            win->input.rebuild_snapshot();
        } else if (win->resolved_scope_id().is_embedded()) {
            const ui::InternedId group_iid = interner_.lookup(scope_key);
            const bp2::Blueprint::Node* node = group_iid.empty()
                ? nullptr
                : model_.current().find_node(group_iid);

            if (node && node->has_embedded_blueprint() && node->blueprint_instance().source.inline_def()) {
                visual::mutations::rebuild(win->scene, *node->blueprint_instance().source.inline_def(),
                                           interner_, arena_, scope_key, reg,
                                           &runtime_node_states_, &session_node_appearance_);
                win->input.rebuild_snapshot();
            } else {
                spdlog::error("[editor] Embedded window '{}' missing embedded blueprint during rebuild", scope_key);
                continue;
            }
        } else {
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, scope_key, reg,
                                       &runtime_node_states_, &session_node_appearance_);
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

        overlay_simulation_values(content, n, interner_, type_name, nid, simulation_);
        const editor::NodeInstanceKey key = editor::make_node_instance_key(interner_, sim_id_prefix, n.semantic.id);
        switch (content.type) {
            case bp2::NodeContentType::Slider:
            case bp2::NodeContentType::Gauge:
            case bp2::NodeContentType::Indicator:
                runtime_node_states_[key] = editor::ScalarNodeRuntimeState{content.value};
                break;
            case bp2::NodeContentType::Knob:
                runtime_node_states_[key] = editor::DiscreteNodeRuntimeState{static_cast<int>(content.value)};
                break;
            case bp2::NodeContentType::Switch:
            case bp2::NodeContentType::VerticalToggle:
                runtime_node_states_[key] = (type_name == "AZS")
                    ? editor::RuntimeNodeState(editor::BoolTrippedNodeRuntimeState{content.state, content.tripped})
                    : editor::RuntimeNodeState(editor::BoolNodeRuntimeState{content.state});
                break;
            default:
                break;
        }
        dispatch_content_to_widget(window_manager_, interner_, n.semantic.id, sim_id_prefix, content);
    };

    // Update root-level nodes
    for (const bp2::Blueprint::Node& n : model_.current().nodes()) {
        update_node_content(n, "");
    }

    // Update nodes inside embedded blueprint instances
    for (const bp2::Blueprint::Node& parent_node : model_.current().nodes()) {
        if (!parent_node.has_embedded_blueprint() || !parent_node.blueprint_instance().source.inline_def()) continue;
        const std::string parent_id = std::string(interner_.resolve(parent_node.semantic.id));
        for (const bp2::Blueprint::Node& inner : parent_node.blueprint_instance().source.inline_def()->nodes()) {
            update_node_content(inner, parent_id);
        }
    }
}

/// Rebuild runtime node states from the current blueprint. Pure state reset —
/// does NOT rebuild window scenes or restart the simulation. Callers that need
/// visual updates must call rebuildAllWindows() or rebuild_window_scenes() explicitly.
void Document::resetNodeContent(const ComponentRegistry& /*registry*/) {
    runtime_node_states_.clear();
    std::vector<ui::InternedId> instance_path;
    editor::walk_blueprint_nodes(model_.current(), instance_path, [&](const bp2::Blueprint::Node& node, std::span<const ui::InternedId> path) {
        const editor::RuntimeNodeState state = build_runtime_state(node, interner_, type_registry_);
        if (!std::holds_alternative<std::monostate>(state)) {
            runtime_node_states_.insert_or_assign(
                editor::make_node_instance_key(path, node.semantic.id), state);
        }
    });
}

void Document::purge_transient_node_state() {
    editor::RuntimeNodeStateStore next_runtime;
    editor::SessionNodeAppearanceStore next_appearance;

    std::vector<ui::InternedId> instance_path;
    editor::walk_blueprint_nodes(model_.current(), instance_path, [&](const bp2::Blueprint::Node& node, std::span<const ui::InternedId> path) {
        const editor::NodeInstanceKey key = editor::make_node_instance_key(path, node.semantic.id);
        if (const auto rt = runtime_node_states_.find(key); rt != runtime_node_states_.end()) {
            next_runtime.emplace(rt->first, rt->second);
        }
        if (const auto ap = session_node_appearance_.find(key); ap != session_node_appearance_.end()) {
            next_appearance.emplace(ap->first, ap->second);
        }
    });

    runtime_node_states_ = std::move(next_runtime);
    session_node_appearance_ = std::move(next_appearance);
}

std::optional<editor::NodeColor> Document::node_color_for_scope(const WindowScopeId& scope_id,
                                                                ui::InternedId node_id) const {
    const editor::NodeInstanceKey key = editor::make_node_instance_key(
        const_cast<ui::StringInterner&>(interner_), scope_id.sim_scope_prefix(), node_id);
    return editor::lookup_node_color(session_node_appearance_, key);
}

std::optional<editor::NodeColor> Document::node_color_for_scope(const std::string& scope_id,
                                                                ui::InternedId node_id) const {
    return node_color_for_scope(scope_id.empty() ? WindowScopeId::root() : WindowScopeId::embedded(scope_id), node_id);
}

void Document::set_node_color_for_scope(const std::string& scope_id,
                                        ui::InternedId node_id,
                                        std::optional<editor::NodeColor> color) {
    const editor::NodeInstanceKey key = editor::make_node_instance_key(interner_, scope_id, node_id);
    if (color.has_value()) {
        session_node_appearance_[key] = *color;
    } else {
        session_node_appearance_.erase(key);
    }
    dispatch_color_to_widget(window_manager_, interner_, node_id, scope_id, color);
}

const bp2::Blueprint::Node* Document::find_node_in_scope(
    const std::string& scope_id, const editor::NodeId& node_id) const {
    const ui::InternedId node_iid = interner_.lookup(node_id.str());
    if (node_iid.empty()) return nullptr;

    if (scope_id.empty()) {
        return model_.current().find_node(node_iid);
    }
    const ui::InternedId group_iid = interner_.lookup(scope_id);
    const bp2::Blueprint::Node* group_node = group_iid.empty()
        ? nullptr : model_.current().find_node(group_iid);
    if (!group_node || !group_node->has_embedded_blueprint()) return nullptr;
    if (const auto* def = group_node->blueprint_instance().source.inline_def()) {
        return def->find_node(node_iid);
    }
    return nullptr;
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

    const bp2::Blueprint::Node* n = find_node_in_scope(scope_id, node_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = value;
    runtime_node_states_[editor::make_node_instance_key(interner_, scope_id, n->semantic.id)] = editor::ScalarNodeRuntimeState{value};
    dispatch_content_to_widget(window_manager_, interner_, n->semantic.id, scope_id, content);
}

void Document::setKnobPosition(const editor::NodeId& node_id, int position, const std::string& scope_id) {
     const std::string sim_id = make_sim_id(node_id, scope_id);
     signal_overrides_[signal_key::make_node_port_key(sim_id, "control")] = static_cast<float>(position);

    const bp2::Blueprint::Node* n = find_node_in_scope(scope_id, node_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = static_cast<float>(position);
    runtime_node_states_[editor::make_node_instance_key(interner_, scope_id, n->semantic.id)] = editor::DiscreteNodeRuntimeState{position};
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
