#include "document.h"
#include "document_simulation_internal.h"

#include "signal_key_resolver.h"
#include "core/solvers/common/signal_key.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include "identity.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <spdlog/spdlog.h>

namespace {

/// Convert a WindowScopeId path to a typed InternedId instance_path.
/// Now just forwards the path since WindowScopeId already stores InternedIds.
std::vector<ui::InternedId> scope_id_to_instance_path(const WindowScopeId& scope_id) {
    return std::vector<ui::InternedId>(scope_id.path().begin(), scope_id.path().end());
}

/// Build a NodeInstanceKey from a WindowScopeId + local node id.
editor::NodeInstanceKey make_scoped_node_instance_key(const WindowScopeId& scope_id,
                                                      ui::InternedId local_node_id) {
    return editor::make_node_instance_key(scope_id_to_instance_path(scope_id), local_node_id);
}

/// Resolve an InternedId for a (sim_node_id, port_name) pair against the simulation interner.
/// Called only at cache-build time (simulation start) and interaction time — never per-frame.
ui::InternedId resolve_port_key(const ui::StringInterner& sim_interner,
                                const std::string& sim_node_id,
                                std::string_view port_name) {
    return sim_interner.lookup(signal_key::make_node_port_key(sim_node_id, port_name));
}

/// std::visit overload set helper — standard C++ idiom for variant dispatch.
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

/// Extract the control InternedId from a ContentPorts variant.
/// Returns empty InternedId for non-interactive content types.
/// Caller must check for .empty() before use.
[[nodiscard]] ui::InternedId extract_control_port(const editor::ContentPorts& ports) {
    return std::visit(overloaded{
        [](std::monostate) { return ui::InternedId{}; },
        [](const editor::GaugePorts&) { return ui::InternedId{}; },
        [](const editor::IndicatorPorts&) { return ui::InternedId{}; },
        [](const editor::SwitchPorts& p) { return p.control; },
        [](const editor::AzsPorts& p) { return p.control; },
        [](const editor::SliderPorts& p) { return p.control; },
        [](const editor::KnobPorts& p) { return p.control; }
    }, ports);
}

/// Overlay simulation values onto NodeContent using variant-dispatched port reads.
/// Zero string construction, zero hash table lookup — pure integer reads.
/// Each content type only accesses its own ports; no cross-contamination.
void overlay_from_cache(NodeContent& content,
                        const editor::NodeSignalCache& cache,
                        const Simulator<JIT_Solver>& simulation) {
    std::visit(overloaded{
        [](std::monostate) {},
        [&](const editor::GaugePorts& p) {
            content.value = simulation.get_signal_value(p.v_in);
        },
        [&](const editor::IndicatorPorts& p) {
            content.value = std::clamp(simulation.get_signal_value(p.brightness), 0.0f, 1.0f);
        },
        [&](const editor::SwitchPorts& p) {
            content.state = simulation.get_signal_value(p.state) > 0.5f;
        },
        [&](const editor::AzsPorts& p) {
            content.state = simulation.get_signal_value(p.state) > 0.5f;
            content.tripped = simulation.get_signal_value(p.tripped) > 0.5f;
        },
        [&](const editor::SliderPorts& p) {
            if (float val = simulation.get_signal_value(p.readback); std::isfinite(val)) {
                content.value = val;
            }
        },
        [&](const editor::KnobPorts& p) {
            if (float val = simulation.get_signal_value(p.position); std::isfinite(val)) {
                content.value = val;
            }
        }
    }, cache.ports);
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

/// Dispatch a node color update to the window matching scope_id.
///
/// **Dual-path color contract (PUSH path):**
/// This function pushes the canonical `node.view.color` directly to the live
/// widget after a mutation, bypassing a full scene rebuild. The PULL path
/// (scene rebuild) reads the same `n.view.color` in `scene_mutations.cpp`.
/// Both paths must produce identical visual results via `NodeColor::to_uint32()`.
/// Any change to the color model or widget color API must update both paths.
void dispatch_color_to_widget(WindowManager& window_manager,
                              ui::StringInterner& interner,
                              ui::InternedId node_iid,
                              const WindowScopeId& scope_id,
                              std::optional<editor::NodeColor> color) {
    BlueprintWindow* win = window_manager.find(scope_id);
    if (!win) return;
    std::string_view node_sv = interner.resolve(node_iid);
    if (auto* widget = win->scene.find(node_sv)) {
        widget->setCustomColor(color.has_value() ? std::optional<uint32_t>(color->to_uint32()) : std::nullopt);
    }
}

/// Dispatch a content update to all matching windows.
void dispatch_content_to_widget(WindowManager& window_manager,
                                ui::StringInterner& interner,
                                ui::InternedId node_iid,
                                const WindowScopeId& scope_id,
                                const NodeContent& content) {
    BlueprintWindow* win = window_manager.find(scope_id);
    if (!win) return;
    std::string_view node_sv = interner.resolve(node_iid);
    auto* widget = win->scene.find(node_sv);
    if (!widget) return;
    auto* nw = dynamic_cast<visual::NodeWidget*>(widget);
    if (nw) nw->updateContent(content);
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

// Path-walking utilities live in embedded_path_utils.cpp.
// document_simulation.cpp only consumes them via the header.

// ============================================================================

} // namespace editor

// ============================================================================
// Document scope resolution
// ============================================================================

Document::ResolvedSignalScope Document::resolve_signal_scope(const WindowScopeId& scope_id) const {
    if (scope_id.is_external()) {
        // For external scopes, use the first path element as parent instance.
        // Multi-segment paths need externally pre-interned keys (not supported here).
        ui::InternedId scope_iid = scope_id.path().empty() ? ui::InternedId{} : scope_id.path()[0];
        if (const BlueprintWindow* win = window_manager_.find(scope_id)) {
            if (win->external_blueprint && win->external_interner) {
                return {
                    &*win->external_blueprint,
                    win->external_interner.get(),
                    editor::external_ref_signal_context(scope_iid)
                };
            }
        }
        return {nullptr, nullptr, editor::external_ref_signal_context(scope_iid)};
    }

    if (scope_id.is_embedded()) {
        // For embedded scopes, use the first path element as parent instance.
        ui::InternedId scope_iid = scope_id.path().empty() ? ui::InternedId{} : scope_id.path()[0];
        if (const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(
                model_.current(), scope_id.path())) {
            return {
                embedded_bp,
                &interner_,
                editor::embedded_signal_context(scope_iid)
            };
        }
        return {nullptr, nullptr, editor::embedded_signal_context(scope_iid)};
    }

    return {&model_.current(), &interner_, editor::root_signal_context()};
}

void Document::rebuild_window_scenes() {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    for (auto& win : window_manager_.windows()) {
        std::vector<ui::InternedId> instance_path = scope_id_to_instance_path(win->resolved_scope_id());

        if (win->is_external_ref() && win->external_blueprint
            && win->external_interner && win->external_arena) {
            visual::mutations::rebuild(win->scene, *win->external_blueprint,
                                       *win->external_interner, *win->external_arena, instance_path, reg,
                                       &runtime_node_states_);
            win->input.rebuild_snapshot();
        } else if (win->resolved_scope_id().is_embedded()) {
            if (const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(
                    model_.current(), win->resolved_scope_id().path())) {
                visual::mutations::rebuild(win->scene, *embedded_bp,
                                           interner_, arena_, instance_path, reg,
                                           &runtime_node_states_);
                win->input.rebuild_snapshot();
            } else {
                spdlog::error("[editor] Embedded window '{}' missing embedded blueprint during rebuild",
                              editor::instance_path_to_scope_string(interner_, win->resolved_scope_id().path()));
                continue;
            }
        } else {
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, instance_path, reg,
                                       &runtime_node_states_);
            win->input.rebuild_snapshot();
        }
    }
}

// ============================================================================
// Simulation lifecycle
// ============================================================================

void Document::startSimulation() {
    if (!simulation_running_) {
        try {
            simulation_.start(build_jit_input());
            build_signal_cache();
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
    signal_cache_.clear();
    typed_overrides_.clear();
    held_buttons_.clear();
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
            build_signal_cache();
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
            signal_cache_.clear();
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
    window_manager_.remove_orphaned_windows();
    rebuild_window_scenes();
    rebuildSimulation();
}

// ===========================================================================
// Signal cache — pre-resolve InternedIds at simulation start
// ===========================================================================

void Document::build_signal_cache() {
    signal_cache_.clear();
    const auto& sim_interner = simulation_.signal_key_interner();

    std::vector<ui::InternedId> instance_path;
    editor::walk_blueprint_nodes(model_.current(), instance_path,
        [&](const bp2::Blueprint::Node& n, std::span<const ui::InternedId> path) {
            const NodeContent base = resolve_base_content(n, interner_, type_registry_);
            if (base.type == bp2::NodeContentType::None) return;

            // Build simulation node ID (string construction here is fine —
            // this runs once at simulation start, not per-frame).
            const std::string sim_id_prefix = editor::instance_path_to_scope_string(interner_, path);
            const std::string local_id = std::string(interner_.resolve(n.semantic.id));
            const std::string nid = sim_id_prefix.empty()
                ? local_id
                : signal_key::make_child_scope_key(sim_id_prefix, local_id);

            // Resolve content-type-specific ports into a discriminated variant.
            // Each branch constructs only the ports relevant to its content type.
            // AZS gets its own variant (AzsPorts) with the tripped port.
            const std::string type_name(interner_.resolve(n.semantic.type));
            editor::ContentPorts ports = std::monostate{};
            switch (base.type) {
                case bp2::NodeContentType::Switch:
                case bp2::NodeContentType::VerticalToggle: {
                    if (type_name == "AZS") {
                        editor::AzsPorts ap;
                        ap.state   = resolve_port_key(sim_interner, nid, "state");
                        ap.control = resolve_port_key(sim_interner, nid, "control");
                        ap.tripped = resolve_port_key(sim_interner, nid, "tripped");
                        ports = ap;
                    } else {
                        editor::SwitchPorts sp;
                        sp.state   = resolve_port_key(sim_interner, nid, "state");
                        sp.control = resolve_port_key(sim_interner, nid, "control");
                        ports = sp;
                    }
                    break;
                }
                case bp2::NodeContentType::Indicator: {
                    editor::IndicatorPorts ip;
                    ip.brightness = resolve_port_key(sim_interner, nid, "brightness");
                    ports = ip;
                    break;
                }
                case bp2::NodeContentType::Gauge: {
                    editor::GaugePorts gp;
                    gp.v_in = resolve_port_key(sim_interner, nid, "v_in");
                    ports = gp;
                    break;
                }
                case bp2::NodeContentType::Slider: {
                    editor::SliderPorts slp;
                    if (auto port = editor::select_slider_readback_port(n, interner_)) {
                        slp.readback = resolve_port_key(sim_interner, nid, *port);
                    }
                    slp.control = resolve_port_key(sim_interner, nid, "control");
                    ports = slp;
                    break;
                }
                case bp2::NodeContentType::Knob: {
                    editor::KnobPorts kp;
                    kp.position = resolve_port_key(sim_interner, nid, "position");
                    kp.control  = resolve_port_key(sim_interner, nid, "control");
                    ports = kp;
                    break;
                }
                default:
                    break;
            }

            // Skip if no ports were resolved (unhandled content type).
            if (std::holds_alternative<std::monostate>(ports)) return;

            const editor::NodeInstanceKey key = editor::make_node_instance_key(path, n.semantic.id);

            // Build the WindowScopeId once — avoids per-frame string construction.
            WindowScopeId scope = WindowScopeId::root();
            if (!path.empty()) {
                scope = WindowScopeId::embedded(std::vector<ui::InternedId>(path.begin(), path.end()));
            }

            signal_cache_[key] = editor::NodeSignalCache{
                .base_content = base,
                .ports = std::move(ports),
                .scope = std::move(scope)
            };
        });
}

// ============================================================================
// Simulation step & content updates
// ============================================================================

void Document::updateSimulationStep(double dt) {
    if (!simulation_running_) return;

    // Merge held-button overrides (pre-resolved InternedIds) with interaction overrides.
    std::vector<std::pair<ui::InternedId, float>> all_overrides;
    all_overrides.reserve(typed_overrides_.size() + held_buttons_.size());

    for (const auto& [key, control_iid] : held_buttons_) {
        if (!control_iid.empty()) all_overrides.push_back({control_iid, 1.0f});
    }
    all_overrides.insert(all_overrides.end(), typed_overrides_.begin(), typed_overrides_.end());

    simulation_.apply_typed_overrides(all_overrides);
    simulation_.step(dt);
    typed_overrides_.clear();
}

void Document::updateNodeContentFromSimulation() {
    if (!simulation_running_) return;

    // Iterate the pre-resolved signal cache — zero string construction.
    // The cache was built at simulation start with all InternedIds resolved.
    for (auto& [key, cache] : signal_cache_) {
        // Start from static base content (has min/max/positions/label/etc.),
        // then overlay live simulation values on top.
        NodeContent content = cache.base_content;
        overlay_from_cache(content, cache, simulation_);

        // Update runtime state — variant dispatch ensures each content type
        // produces the correct state variant.
        std::visit(overloaded{
            [](std::monostate) {},
            [&](const editor::GaugePorts&) {
                runtime_node_states_[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::IndicatorPorts&) {
                runtime_node_states_[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::SliderPorts&) {
                runtime_node_states_[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::KnobPorts&) {
                runtime_node_states_[key] = editor::DiscreteNodeRuntimeState{static_cast<int>(content.value)};
            },
            [&](const editor::SwitchPorts&) {
                runtime_node_states_[key] = editor::BoolNodeRuntimeState{content.state};
            },
            [&](const editor::AzsPorts&) {
                runtime_node_states_[key] = editor::BoolNodeRuntimeState{content.state};
            }
        }, cache.ports);

        // Dispatch to widget using pre-built scope — zero string construction.
        dispatch_content_to_widget(window_manager_, interner_, key.local_node_id, cache.scope, content);
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

    std::vector<ui::InternedId> instance_path;
    editor::walk_blueprint_nodes(model_.current(), instance_path, [&](const bp2::Blueprint::Node& node, std::span<const ui::InternedId> path) {
        const editor::NodeInstanceKey key = editor::make_node_instance_key(path, node.semantic.id);
        if (const auto rt = runtime_node_states_.find(key); rt != runtime_node_states_.end()) {
            next_runtime.emplace(rt->first, rt->second);
        }
    });

    runtime_node_states_ = std::move(next_runtime);
}

// ============================================================================
// Per-node scope queries
// ============================================================================

std::optional<editor::NodeColor> Document::node_color_for_scope(const WindowScopeId& scope_id,
                                                                 ui::InternedId node_id) const {
    if (scope_id.is_external()) {
        return std::nullopt;
    }

    if (scope_id.is_root()) {
        const auto* node = model_.current().find_node(node_id);
        return node ? node->view.color : std::nullopt;
    }

    const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(model_.current(), scope_id.path());
    if (!embedded_bp) {
        return std::nullopt;
    }
    const auto* node = embedded_bp->find_node(node_id);
    return node ? node->view.color : std::nullopt;
}

void Document::set_node_color_for_scope(const WindowScopeId& scope_id,
                                        ui::InternedId node_id,
                                        std::optional<editor::NodeColor> color) {
    if (scope_id.is_external()) {
        return;
    }

    const std::optional<editor::NodeColor> canonical_color = color.has_value()
        ? std::optional<editor::NodeColor>(editor::NodeColor::canonicalized(*color))
        : std::nullopt;

    auto assign_color = [canonical_color](bp2::Blueprint::Node& node) {
        node.view.color = canonical_color;
    };

    bool resolved_target = false;

    if (scope_id.is_root()) {
        resolved_target = (model_.current().find_node(node_id) != nullptr);
        if (!resolved_target) {
            return;
        }
        model_.update_node(node_id, assign_color);
    } else {
        const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(model_.current(), scope_id.path());
        resolved_target = embedded_bp && (embedded_bp->find_node(node_id) != nullptr);
        if (!resolved_target) {
            return;
        }

        // scope_id.path() already returns InternedId vector - use directly
        model_.update_embedded_node(scope_id.path(), node_id, assign_color);
    }

    if (resolved_target) {
        dispatch_color_to_widget(window_manager_, interner_, node_id, scope_id, canonical_color);
    }
}

const bp2::Blueprint::Node* Document::find_node_in_scope(
    const WindowScopeId& scope_id, const editor::NodeId& node_id) const {
    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    if (!resolved.blueprint || !resolved.interner) {
        return nullptr;
    }

    const ui::InternedId node_iid = resolved.interner->lookup(node_id.str());
    if (node_iid.empty()) return nullptr;
    return resolved.blueprint->find_node(node_iid);
}

// ============================================================================
// Signal key resolution
// ============================================================================

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
        ui::InternedId port_iid = editor::resolve_runtime_signal_key(
            *resolved.blueprint, *resolved.interner, simulation_.signal_key_interner(), endpoint, resolved.context);
        if (port_iid.empty()) continue;

        if (std::abs(simulation_.get_signal_value(port_iid)) > 0.5f) {
            out.insert(resolved.interner->resolve(w.id));
        }
    }
}

ui::InternedId Document::resolve_endpoint_signal_key(const WindowScopeId& scope_id,
                                                  std::string_view node_id,
                                                  std::string_view port_name) const {
    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    if (!resolved.blueprint || !resolved.interner) {
        return {};
    }

    const ui::InternedId node_iid = resolved.interner->lookup(node_id);
    const ui::InternedId port_iid = resolved.interner->lookup(port_name);
    if (node_iid.empty() || port_iid.empty()) {
        return {};
    }

    const bp2::Blueprint::Node* node = resolved.blueprint->find_node(node_iid);
    const editor::SignalEndpoint endpoint{node, node_iid, port_iid};
    return editor::resolve_runtime_signal_key(
        *resolved.blueprint, *resolved.interner, simulation_.signal_key_interner(), endpoint, resolved.context);
}

ui::InternedId Document::resolve_wire_signal_key(const WindowScopeId& scope_id,
                                               std::string_view wire_id) const {
    const ResolvedSignalScope resolved = resolve_signal_scope(scope_id);
    const auto [wire, resolved_wire_id] = find_wire_in_scope(resolved, wire_id);
    if (!wire || resolved_wire_id.empty()) {
        return {};
    }

    if (wire->source.node.empty() || wire->source.port.empty()) {
        return {};
    }

    const bp2::Blueprint::Node* node = resolved.blueprint->find_node(wire->source.node);
    const editor::SignalEndpoint endpoint{node, wire->source.node, wire->source.port};
    return editor::resolve_runtime_signal_key(
        *resolved.blueprint, *resolved.interner, simulation_.signal_key_interner(), endpoint, resolved.context);
}

// ============================================================================
// Signal overrides (switch/button/knob interaction)
// ============================================================================

void Document::triggerSwitch(const editor::NodeId& node_id, const WindowScopeId& scope_id) {
    const auto key = make_scoped_node_instance_key(scope_id, interner_.lookup(node_id.str()));
    const auto it = signal_cache_.find(key);
    if (it == signal_cache_.end()) return;

    const ui::InternedId control_key = extract_control_port(it->second.ports);
    if (control_key.empty()) return;

    float current = simulation_.get_signal_value(control_key);
    float next = (current < 0.5f) ? 1.0f : 0.0f;
    typed_overrides_.push_back({control_key, next});
}

void Document::setSliderValue(const editor::NodeId& node_id, float value, const WindowScopeId& scope_id) {
    const auto key = make_scoped_node_instance_key(scope_id, interner_.lookup(node_id.str()));
    const auto it = signal_cache_.find(key);

    // Send override to simulation if the node has a cached control port.
    if (it != signal_cache_.end()) {
        const ui::InternedId control_key = extract_control_port(it->second.ports);
        if (!control_key.empty()) {
            typed_overrides_.push_back({control_key, value});
        }
    }

    const bp2::Blueprint::Node* n = find_node_in_scope(scope_id, node_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = value;
    runtime_node_states_[make_scoped_node_instance_key(scope_id, n->semantic.id)] = editor::ScalarNodeRuntimeState{value};
    dispatch_content_to_widget(window_manager_, interner_, n->semantic.id, scope_id, content);
}

void Document::setKnobPosition(const editor::NodeId& node_id, int position, const WindowScopeId& scope_id) {
    const auto key = make_scoped_node_instance_key(scope_id, interner_.lookup(node_id.str()));
    const auto it = signal_cache_.find(key);

    // Send override to simulation if the node has a cached control port.
    if (it != signal_cache_.end()) {
        const ui::InternedId control_key = extract_control_port(it->second.ports);
        if (!control_key.empty()) {
            typed_overrides_.push_back({control_key, static_cast<float>(position)});
        }
    }

    const bp2::Blueprint::Node* n = find_node_in_scope(scope_id, node_id);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, interner_, type_registry_);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = static_cast<float>(position);
    runtime_node_states_[make_scoped_node_instance_key(scope_id, n->semantic.id)] = editor::DiscreteNodeRuntimeState{position};
    dispatch_content_to_widget(window_manager_, interner_, n->semantic.id, scope_id, content);
}

void Document::holdButtonPress(const editor::NodeId& node_id, const WindowScopeId& scope_id) {
    const auto key = make_scoped_node_instance_key(scope_id, interner_.lookup(node_id.str()));
    const auto it = signal_cache_.find(key);
    if (it == signal_cache_.end()) return;

    const ui::InternedId control_key = extract_control_port(it->second.ports);
    if (control_key.empty()) return;

    held_buttons_[key] = control_key;
}

void Document::holdButtonRelease(const editor::NodeId& node_id, const WindowScopeId& scope_id) {
    const auto key = make_scoped_node_instance_key(scope_id, interner_.lookup(node_id.str()));
    auto it = held_buttons_.find(key);
    if (it != held_buttons_.end()) {
        if (!it->second.empty()) {
            typed_overrides_.push_back({it->second, 2.0f});
        }
        held_buttons_.erase(it);
    }
}
