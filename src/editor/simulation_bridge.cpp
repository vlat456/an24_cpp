#include "simulation_bridge.h"
#include "scope_resolver.h"
#include "document_simulation_internal.h"

#include "core/solvers/jit/simulator.h"
#include "core/solvers/common/signal_key.h"
#include "visual/node/visual_node.h"
#include "identity.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/library_index.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/blueprint/embedded_mutation.h"
#include "core/model/component_registry.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <spdlog/spdlog.h>

// ============================================================================
// PIMPL — all private state lives here
// ============================================================================

struct SimulationBridge::Impl {
    bp2::EditorModel& model;
    WindowManager& window_manager;
    core::StringInterner& interner;
    bp2::PathArena& arena;
    const ComponentRegistry* type_registry = nullptr;

    Simulator<JIT_Solver> simulation;
    bool running = false;

    // Pre-resolved signal caches — built at simulation start, zero allocation per frame.
    editor::SignalCache signal_cache;
    editor::WireSignalCache wire_signal_cache;

    // Interaction overrides — InternedIds resolved once at interaction time.
    std::vector<std::pair<core::InternedId, float>> typed_overrides;

    // Persistent merge buffer — capacity preserved across frames.
    std::vector<std::pair<core::InternedId, float>> override_buffer;

    // Held buttons — key is NodeInstanceKey, value is pre-resolved control port InternedId.
    std::unordered_map<editor::NodeInstanceKey, core::InternedId, editor::NodeInstanceKeyHash> held_buttons;

    // Live runtime node states (slider positions, switch states, etc.).
    editor::RuntimeNodeStateStore runtime_node_states;

    // ── Internal helpers ──

    void build_signal_cache();

    /// Extract (node_id, port_name) from a bp2::Path.
    std::pair<core::InternedId, core::InternedId>
    bp2_path_to_node_port(const bp2::Path& path) const;

    /// Overload for WireEndpoint.
    std::pair<core::InternedId, core::InternedId>
    bp2_path_to_node_port(const bp2::WireEndpoint& ep) const;
};

// ============================================================================
// Local helpers
// ============================================================================

namespace {

// Button hold/release signal values — consumed by component control ports.
// Press=1 (active while held), Release=2 (one-shot release event consumed by component).
constexpr float BUTTON_HOLD_VALUE  = 1.0f;
constexpr float BUTTON_RELEASE_VALUE = 2.0f;

/// Build a NodeInstanceKey from a WindowScopeId + local node id.
editor::NodeInstanceKey make_scoped_node_instance_key(const WindowScopeId& scope_id,
                                                      core::InternedId local_node_id) {
    return editor::make_node_instance_key(editor::scope_id_to_instance_path(scope_id), local_node_id);
}

/// Resolve an InternedId for a (sim_node_id, port_name) pair against the simulation interner.
/// Called only at cache-build time (simulation start) and interaction time — never per-frame.
core::InternedId resolve_port_key(const core::StringInterner& sim_interner,
                                  const std::string& sim_node_id,
                                  std::string_view port_name) {
    return sim_interner.lookup(signal_key::make_node_port_key(sim_node_id, port_name));
}

/// std::visit overload set helper — standard C++ idiom for variant dispatch.
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

/// Extract the control InternedId from a ContentPorts variant.
/// Returns empty InternedId for non-interactive content types.
[[nodiscard]] core::InternedId extract_control_port(const editor::ContentPorts& ports) {
    return std::visit(overloaded{
        [](std::monostate) { return core::InternedId{}; },
        [](const editor::GaugePorts&) { return core::InternedId{}; },
        [](const editor::IndicatorPorts&) { return core::InternedId{}; },
        [](const editor::SwitchPorts& p) { return p.control; },
        [](const editor::SliderPorts& p) { return p.control; },
        [](const editor::KnobPorts& p) { return p.control; }
    }, ports);
}

/// Overlay simulation values onto NodeContent using variant-dispatched port reads.
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
    const editor::ResolvedScope& resolved,
    std::string_view wire_id) {
    if (!resolved.blueprint || !resolved.interner) {
        return {nullptr, {}};
    }

    const core::InternedId wire_iid = resolved.interner->lookup(wire_id);
    if (wire_iid.empty()) {
        return {nullptr, {}};
    }

    return {resolved.blueprint->find_wire(wire_iid), resolved.interner->resolve(wire_iid)};
}

NodeContent resolve_base_content(const bp2::Blueprint::Node& node,
                                  core::StringInterner& interner,
                                  const ComponentRegistry* registry) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const auto* def = registry ? registry->get(type_name) : nullptr;
    const TypePresentation* pres = registry ? registry->get_presentation(type_name) : nullptr;
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
                                              core::StringInterner& interner,
                                              const ComponentRegistry* registry) {
    const NodeContent content = resolve_base_content(node, interner, registry);
    return default_runtime_state(content.type, content);
}

/// Dispatch a content update to the window matching scope_id.
void dispatch_content_to_widget(WindowManager& window_manager,
                                core::StringInterner& interner,
                                core::InternedId node_iid,
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

/// Build a BlueprintLibrary from the ComponentRegistry (composite blueprints).
bp2::BlueprintLibrary build_library(
    const bp2::LibraryIndex* library_index,
    const ComponentRegistry* type_registry,
    core::StringInterner& interner) {

    bp2::BlueprintLibrary library;
    if (library_index && type_registry) {
        for (const auto& [classname, spec] : type_registry->all_types()) {
            if (!is_composite(spec)) continue;
            bp2::Blueprint loaded;
            try {
                loaded = bp2::blueprint_from_type_definition(spec, interner, *type_registry);
            } catch (const std::exception& e) {
                spdlog::warn("[editor] export flatten: failed to build blueprint '{}' from ComponentSpec: {}",
                             classname, e.what());
                continue;
            }
            library.add(interner.intern(classname), std::move(loaded));
        }
    }
    return library;
}

} // namespace

namespace editor {

std::optional<std::string_view> select_slider_readback_port(const bp2::Blueprint::Node& node,
                                                             core::StringInterner& interner) {
    if (node.component().iface.has(interner.intern("out"))) {
        return std::string_view{"out"};
    }
    if (node.component().iface.has(interner.intern("control"))) {
        return std::string_view{"control"};
    }
    return std::nullopt;
}

void walk_blueprint_nodes(const bp2::Blueprint& bp,
                          std::vector<core::InternedId>& instance_path,
                          const std::function<void(const bp2::Blueprint::Node&, std::span<const core::InternedId>)>& fn) {
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

// ============================================================================
// PIMPL internal methods
// ============================================================================

void SimulationBridge::Impl::build_signal_cache() {
    signal_cache.clear();
    const auto& sim_interner = simulation.signal_key_interner();

    std::vector<core::InternedId> instance_path;
    editor::walk_blueprint_nodes(model.current(), instance_path,
        [&](const bp2::Blueprint::Node& n, std::span<const core::InternedId> path) {
            const NodeContent base = resolve_base_content(n, interner, type_registry);
            if (base.type == bp2::NodeContentType::None) return;

            const std::string sim_id_prefix = editor::instance_path_to_scope_string(interner, path);
            const std::string local_id = std::string(interner.resolve(n.semantic.id));
            const std::string nid = sim_id_prefix.empty()
                ? local_id
                : signal_key::make_child_scope_key(sim_id_prefix, local_id);

            editor::ContentPorts ports = std::monostate{};
            switch (base.type) {
                case bp2::NodeContentType::Switch:
                case bp2::NodeContentType::VerticalToggle: {
                    editor::SwitchPorts sp;
                    sp.state   = resolve_port_key(sim_interner, nid, "state");
                    sp.control = resolve_port_key(sim_interner, nid, "control");
                    ports = sp;
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
                    if (auto port = editor::select_slider_readback_port(n, interner)) {
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

            if (std::holds_alternative<std::monostate>(ports)) return;

            const editor::NodeInstanceKey key = editor::make_node_instance_key(path, n.semantic.id);

            WindowScopeId scope = WindowScopeId::root();
            if (!path.empty()) {
                scope = WindowScopeId::embedded(std::vector<core::InternedId>(path.begin(), path.end()));
            }

            signal_cache[key] = editor::NodeSignalCache{
                .base_content = base,
                .ports = std::move(ports),
                .scope = std::move(scope)
            };
        });

    // Pre-resolve wire energization signal InternedIds for ALL scopes.
    wire_signal_cache.clear();
    for (auto& win : window_manager.windows()) {
        const WindowScopeId& scope_id = win->resolved_scope_id();
        const editor::ResolvedScope resolved = editor::resolve_scope(scope_id, model, window_manager, interner);
        if (!resolved.blueprint || !resolved.interner) continue;

        for (const bp2::Blueprint::Wire& w : resolved.blueprint->wires()) {
            auto [src_node_id, src_port_id] = bp2_path_to_node_port(w.source);
            if (src_node_id.empty() || src_port_id.empty()) continue;

            const bp2::Blueprint::Node* node = resolved.blueprint->find_node(src_node_id);
            editor::SignalEndpoint endpoint{node, src_node_id, src_port_id};
            core::InternedId signal_iid = editor::resolve_runtime_signal_key(
                *resolved.blueprint, *resolved.interner, sim_interner, endpoint, resolved.context);
            if (!signal_iid.empty()) {
                wire_signal_cache[w.id] = signal_iid;
            }
        }
    }
}

std::pair<core::InternedId, core::InternedId>
SimulationBridge::Impl::bp2_path_to_node_port(const bp2::Path& path) const {
    if (path.kind() != bp2::PathKind::Port) return {};
    core::InternedId port_name = path.segment();
    bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    core::InternedId node_id = parent.segment();
    return {node_id, port_name};
}

std::pair<core::InternedId, core::InternedId>
SimulationBridge::Impl::bp2_path_to_node_port(const bp2::WireEndpoint& ep) const {
    return {ep.node, ep.port};
}

// ============================================================================
// SimulationBridge — public API (thin forwarding to Impl)
// ============================================================================

SimulationBridge::SimulationBridge(bp2::EditorModel& model,
                                   WindowManager& window_manager,
                                   core::StringInterner& interner,
                                   bp2::PathArena& arena)
    : impl_(std::make_unique<Impl>(Impl{
          .model = model,
          .window_manager = window_manager,
          .interner = interner,
          .arena = arena,
          .type_registry = nullptr,
          .running = false
      })) {}

SimulationBridge::~SimulationBridge() = default;

// ── Lifecycle ──

void SimulationBridge::start(const JitBuildInput& input) {
    auto& i = *impl_;
    if (!i.running) {
        try {
            i.simulation.start(input);
            i.build_signal_cache();
            i.running = true;
            set_windows_simulation_mode(true);
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to start simulation: {}", e.what());
            i.simulation.stop();
        }
    }
}

void SimulationBridge::stop() {
    auto& i = *impl_;
    i.simulation.stop();
    i.signal_cache.clear();
    i.wire_signal_cache.clear();
    i.typed_overrides.clear();
    i.held_buttons.clear();
    i.running = false;
    set_windows_simulation_mode(false);
}

void SimulationBridge::rebuild(const JitBuildInput& input) {
    auto& i = *impl_;
    if (i.running) {
        i.simulation.stop();
        try {
            i.simulation.start(input);
            i.build_signal_cache();
        } catch (const std::runtime_error& e) {
            spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
            i.signal_cache.clear();
            i.running = false;
            set_windows_simulation_mode(false);
        }
    }
}

[[nodiscard]] bool SimulationBridge::is_running() const {
    return impl_->running;
}

// ── Per-frame ──

void SimulationBridge::step(double dt) {
    auto& i = *impl_;
    if (!i.running) return;

    i.override_buffer.clear();
    for (const auto& [key, control_iid] : i.held_buttons) {
        if (!control_iid.empty()) i.override_buffer.push_back({control_iid, BUTTON_HOLD_VALUE});
    }
    i.override_buffer.insert(i.override_buffer.end(), i.typed_overrides.begin(), i.typed_overrides.end());

    i.simulation.apply_typed_overrides(i.override_buffer);
    i.simulation.step(dt);
    i.typed_overrides.clear();
}

void SimulationBridge::update_node_content() {
    auto& i = *impl_;
    if (!i.running) return;

    for (auto& [key, cache] : i.signal_cache) {
        NodeContent content = cache.base_content;
        overlay_from_cache(content, cache, i.simulation);

        std::visit(overloaded{
            [](std::monostate) {},
            [&](const editor::GaugePorts&) {
                i.runtime_node_states[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::IndicatorPorts&) {
                i.runtime_node_states[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::SliderPorts&) {
                i.runtime_node_states[key] = editor::ScalarNodeRuntimeState{content.value};
            },
            [&](const editor::KnobPorts&) {
                i.runtime_node_states[key] = editor::DiscreteNodeRuntimeState{static_cast<int>(content.value)};
            },
            [&](const editor::SwitchPorts&) {
                i.runtime_node_states[key] = editor::BoolNodeRuntimeState{content.state};
            }
        }, cache.ports);

        dispatch_content_to_widget(i.window_manager, i.interner, key.local_node_id, cache.scope, content);
    }
}

// ── JitBuildInput factory ──

[[nodiscard]] JitBuildInput SimulationBridge::build_jit_input(
    const ComponentRegistry* type_registry,
    const bp2::LibraryIndex* library_index) {
    auto& i = *impl_;
    const bp2::Blueprint& bp = i.model.current();

    bp2::BlueprintLibrary library = build_library(library_index, type_registry, i.interner);

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, i.arena);
    return bp2::elaboration::elaborate_for_jit(netlist, i.arena, i.interner, *type_registry);
}

// ── Interaction ──

void SimulationBridge::trigger_switch(core::InternedId node_id, const WindowScopeId& scope_id) {
    auto& i = *impl_;
    const auto key = make_scoped_node_instance_key(scope_id, node_id);
    const auto it = i.signal_cache.find(key);
    if (it == i.signal_cache.end()) return;

    const core::InternedId control_key = extract_control_port(it->second.ports);
    if (control_key.empty()) return;

    float current = i.simulation.get_signal_value(control_key);
    float next = (current < 0.5f) ? 1.0f : 0.0f;
    i.typed_overrides.push_back({control_key, next});
}

void SimulationBridge::set_slider_value(core::InternedId node_id, float value, const WindowScopeId& scope_id) {
    auto& i = *impl_;
    const auto key = make_scoped_node_instance_key(scope_id, node_id);
    const auto it = i.signal_cache.find(key);

    if (it != i.signal_cache.end()) {
        const core::InternedId control_key = extract_control_port(it->second.ports);
        if (!control_key.empty()) {
            i.typed_overrides.push_back({control_key, value});
        }
    }

    const bp2::Blueprint::Node* n = editor::find_node_in_scope(scope_id, node_id, i.model, i.window_manager, i.interner);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, i.interner, i.type_registry);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = value;
    i.runtime_node_states[make_scoped_node_instance_key(scope_id, n->semantic.id)] = editor::ScalarNodeRuntimeState{value};
    dispatch_content_to_widget(i.window_manager, i.interner, n->semantic.id, scope_id, content);
}

void SimulationBridge::set_knob_position(core::InternedId node_id, int position, const WindowScopeId& scope_id) {
    auto& i = *impl_;
    const auto key = make_scoped_node_instance_key(scope_id, node_id);
    const auto it = i.signal_cache.find(key);

    if (it != i.signal_cache.end()) {
        const core::InternedId control_key = extract_control_port(it->second.ports);
        if (!control_key.empty()) {
            i.typed_overrides.push_back({control_key, static_cast<float>(position)});
        }
    }

    const bp2::Blueprint::Node* n = editor::find_node_in_scope(scope_id, node_id, i.model, i.window_manager, i.interner);
    if (!n) return;

    NodeContent content = resolve_base_content(*n, i.interner, i.type_registry);
    if (content.type == bp2::NodeContentType::None) return;
    content.value = static_cast<float>(position);
    i.runtime_node_states[make_scoped_node_instance_key(scope_id, n->semantic.id)] = editor::DiscreteNodeRuntimeState{position};
    dispatch_content_to_widget(i.window_manager, i.interner, n->semantic.id, scope_id, content);
}

void SimulationBridge::hold_button_press(core::InternedId node_id, const WindowScopeId& scope_id) {
    auto& i = *impl_;
    const auto key = make_scoped_node_instance_key(scope_id, node_id);
    const auto it = i.signal_cache.find(key);
    if (it == i.signal_cache.end()) return;

    const core::InternedId control_key = extract_control_port(it->second.ports);
    if (control_key.empty()) return;

    i.held_buttons[key] = control_key;
}

void SimulationBridge::hold_button_release(core::InternedId node_id, const WindowScopeId& scope_id) {
    auto& i = *impl_;
    const auto key = make_scoped_node_instance_key(scope_id, node_id);
    auto it = i.held_buttons.find(key);
    if (it != i.held_buttons.end()) {
        if (!it->second.empty()) {
            i.typed_overrides.push_back({it->second, BUTTON_RELEASE_VALUE});
        }
        i.held_buttons.erase(it);
    }
}

std::vector<std::pair<core::InternedId, float>>& SimulationBridge::typed_overrides() {
    return impl_->typed_overrides;
}

// ── Signal queries ──

float SimulationBridge::get_signal_value(core::InternedId key) const {
    return impl_->simulation.get_signal_value(key);
}

const core::StringInterner& SimulationBridge::signal_key_interner() const {
    return impl_->simulation.signal_key_interner();
}

// ── Scope-based signal key resolution ──

core::InternedId SimulationBridge::resolve_endpoint_signal_key(const WindowScopeId& scope_id,
                                                                std::string_view node_id,
                                                                std::string_view port_name) const {
    auto& i = *impl_;
    const editor::ResolvedScope resolved = editor::resolve_scope(scope_id, i.model, i.window_manager, i.interner);
    if (!resolved.blueprint || !resolved.interner) {
        return {};
    }

    const core::InternedId node_iid = resolved.interner->lookup(node_id);
    const core::InternedId port_iid = resolved.interner->lookup(port_name);
    if (node_iid.empty() || port_iid.empty()) {
        return {};
    }

    const bp2::Blueprint::Node* node = resolved.blueprint->find_node(node_iid);
    const editor::SignalEndpoint endpoint{node, node_iid, port_iid};
    return editor::resolve_runtime_signal_key(
        *resolved.blueprint, *resolved.interner, i.simulation.signal_key_interner(), endpoint, resolved.context);
}

core::InternedId SimulationBridge::resolve_wire_signal_key(const WindowScopeId& scope_id,
                                                            std::string_view wire_id) const {
    auto& i = *impl_;
    const editor::ResolvedScope resolved = editor::resolve_scope(scope_id, i.model, i.window_manager, i.interner);
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
        *resolved.blueprint, *resolved.interner, i.simulation.signal_key_interner(), endpoint, resolved.context);
}

// ── Energized wire set ──

void SimulationBridge::build_energized_wire_set(
    std::unordered_set<std::string_view, visual::StringViewHash>& out,
    const WindowScopeId& scope_id) const {
    auto& i = *impl_;
    out.clear();
    if (!i.running) return;

    const editor::ResolvedScope resolved = editor::resolve_scope(scope_id, i.model, i.window_manager, i.interner);
    if (!resolved.blueprint || !resolved.interner) {
        return;
    }

    for (const bp2::Blueprint::Wire& w : resolved.blueprint->wires()) {
        auto it = i.wire_signal_cache.find(w.id);
        if (it == i.wire_signal_cache.end()) continue;

        if (std::abs(i.simulation.get_signal_value(it->second)) > 0.5f) {
            out.insert(resolved.interner->resolve(w.id));
        }
    }
}

// ── Runtime node states ──

const editor::RuntimeNodeStateStore& SimulationBridge::runtime_node_states() const {
    return impl_->runtime_node_states;
}

void SimulationBridge::reset_node_content() {
    auto& i = *impl_;
    i.runtime_node_states.clear();
    std::vector<core::InternedId> instance_path;
    editor::walk_blueprint_nodes(i.model.current(), instance_path, [&](const bp2::Blueprint::Node& node, std::span<const core::InternedId> path) {
        const editor::RuntimeNodeState state = build_runtime_state(node, i.interner, i.type_registry);
        if (!std::holds_alternative<std::monostate>(state)) {
            i.runtime_node_states.insert_or_assign(
                editor::make_node_instance_key(path, node.semantic.id), state);
        }
    });
}

void SimulationBridge::purge_transient_node_state() {
    auto& i = *impl_;
    editor::RuntimeNodeStateStore next_runtime;

    std::vector<core::InternedId> instance_path;
    editor::walk_blueprint_nodes(i.model.current(), instance_path, [&](const bp2::Blueprint::Node& node, std::span<const core::InternedId> path) {
        const editor::NodeInstanceKey key = editor::make_node_instance_key(path, node.semantic.id);
        if (const auto rt = i.runtime_node_states.find(key); rt != i.runtime_node_states.end()) {
            next_runtime.emplace(rt->first, rt->second);
        }
    });

    i.runtime_node_states = std::move(next_runtime);
}

// ── Configuration ──

void SimulationBridge::set_type_registry(const ComponentRegistry* reg) {
    impl_->type_registry = reg;
}

void SimulationBridge::set_windows_simulation_mode(bool running) {
    for (auto& win : impl_->window_manager.windows()) {
        win->set_simulation_mode(running);
    }
}
