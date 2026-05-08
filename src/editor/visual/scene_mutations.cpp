/// scene_mutations.cpp — Phase 8: scene rebuild using bp2::Blueprint.
///
/// The rebuild() function is fully migrated to bp2::Blueprint.
/// Other mutation functions (add_node, remove_nodes, etc.) have been
/// removed from the public API; call-sites now use Document::applyInputResult
/// and the command system instead.

#include "scene_mutations.h"
#include "scene.h"
#include "node/node_factory.h"
#include "node/bus_node_widget.h"
#include "node/ref_node_widget.h"
#include "node/visual_node.h"
#include "wire/wire.h"
#include "wire/routing_point.h"
#include "editor/visual/presentation/node_presentation.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/model/component_registry.h"
#include "core/strings/interned_id.h"
#include "visual/snap.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <unordered_map>

namespace visual::mutations {

// side_from_relative_position() and path_to_node_port() live in editor_math (snap.h)

// ============================================================================
// Bus-wire ref helpers
// ============================================================================

/// Build a bus-wire reference from a bp2::Blueprint::Wire.
/// Returns std::nullopt if the source/target paths are malformed.
static std::optional<BusWireRef> to_bus_wire_ref(const bp2::Blueprint::Wire& w,
                                                  const bp2::PathArena& arena) {
    auto [src_node, src_port] = editor_math::path_to_node_port(w.source, arena);
    auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, arena);
    if (src_node.empty() || src_port.empty() || tgt_node.empty() || tgt_port.empty()) {
        return std::nullopt;
    }
    return BusWireRef{w.id, src_node, tgt_node};
}

std::vector<BusWireRef> build_bus_wires(const bp2::Blueprint& bp,
                                         const bp2::PathArena& arena) {
    std::vector<BusWireRef> bus_wires;
    bus_wires.reserve(bp.wires().size());
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        std::optional<BusWireRef> bw = to_bus_wire_ref(w, arena);
        if (bw) bus_wires.push_back(*bw);
    }
    return bus_wires;
}

// ============================================================================
// Wire widget helpers
// ============================================================================

/// Resolve a node/port endpoint to a visual::Port* in the scene.
static Port* resolve_port(const Scene& scene,
                           core::InternedId node_id,
                           core::InternedId port_name,
                           core::InternedId wire_id,
                           const core::StringInterner& interner) {
    std::string_view const node_sv = interner.resolve(node_id);
    Widget const* widget = scene.find(node_sv);
    if (!widget) return nullptr;
    std::string_view const port_sv  = interner.resolve(port_name);
    std::string_view const wire_sv  = interner.resolve(wire_id);
    return widget->portByName(port_sv, wire_sv);
}

std::unique_ptr<Wire> create_wire_widget(const bp2::Blueprint::Wire& w,
                                          const bp2::PathArena& arena,
                                          const core::StringInterner& interner,
                                          const Scene& scene) {
    auto [src_node_id, src_port] = editor_math::path_to_node_port(w.source, arena);
    auto [tgt_node_id, tgt_port] = editor_math::path_to_node_port(w.target, arena);
    if (src_node_id.empty() || src_port.empty() || tgt_node_id.empty() || tgt_port.empty()) {
        return nullptr;
    }

    Port const* start_port = resolve_port(scene, src_node_id, src_port, w.id, interner);
    Port const* end_port   = resolve_port(scene, tgt_node_id, tgt_port, w.id, interner);
    if (!start_port || !end_port) return nullptr;

    std::string_view const wire_id_sv    = interner.resolve(w.id);
    std::string_view const start_node_sv = interner.resolve(src_node_id);
    std::string_view const start_port_sv = interner.resolve(src_port);
    std::string_view const end_node_sv   = interner.resolve(tgt_node_id);
    std::string_view const end_port_sv   = interner.resolve(tgt_port);

    auto wire_widget = std::make_unique<visual::Wire>(
        w.id,
        wire_id_sv,
        start_node_sv, start_port_sv,
        end_node_sv,   end_port_sv);

    for (size_t i = 0; i < w.routing_points.size(); ++i) {
        wire_widget->addRoutingPoint(ui::Pt(w.routing_points[i].first, w.routing_points[i].second), i);
    }

    return wire_widget;
}

// ============================================================================
// Ref/Value node port orientation
// ============================================================================

void orient_ref_node_ports(Scene& scene,
                            const bp2::Blueprint& bp,
                            const bp2::PathArena& arena,
                            const core::StringInterner& interner,
                            std::span<const core::InternedId> /*instance_path*/,
                            const ComponentRegistry& registry) {
    using editor::presentation::NodeFrameKind;
    std::unordered_map<core::InternedId, core::InternedId> ref_to_connected;

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node_id, _src_port] = editor_math::path_to_node_port(w.source, arena);
        auto [tgt_node_id, _tgt_port] = editor_math::path_to_node_port(w.target, arena);
        if (src_node_id.empty() || tgt_node_id.empty()) continue;

         const bp2::Blueprint::Node* src_node = bp.find_node(src_node_id);
         const bp2::Blueprint::Node* tgt_node = bp.find_node(tgt_node_id);
         if (!src_node || !tgt_node) continue;

         const std::string src_type(interner.resolve(src_node->semantic.type));
         const std::string tgt_type(interner.resolve(tgt_node->semantic.type));
         auto src_kind = editor::presentation::resolve_frame_kind(
             registry.get(src_type), registry.get_presentation(src_type));
         auto tgt_kind = editor::presentation::resolve_frame_kind(
             registry.get(tgt_type), registry.get_presentation(tgt_type));

         if (src_kind == NodeFrameKind::Reference && ref_to_connected.count(src_node_id) == 0) {
             ref_to_connected.emplace(src_node_id, tgt_node_id);
         }
         if (tgt_kind == NodeFrameKind::Reference && ref_to_connected.count(tgt_node_id) == 0) {
             ref_to_connected.emplace(tgt_node_id, src_node_id);
         }
    }

    for (const auto& [ref_id, other_id] : ref_to_connected) {
        Widget* ref_widget = scene.find(interner.resolve(ref_id));
        Widget const* other_widget = scene.find(interner.resolve(other_id));
        if (!ref_widget || !other_widget) continue;

        auto* ref_node = (ref_widget->kind() == ui::WidgetKind::RefNode)
                         ? static_cast<RefNodeWidget*>(ref_widget) : nullptr;
        if (!ref_node) continue;

        const Pt ref_pos = ref_widget->worldPos();
        const Pt ref_size = ref_widget->size();
        const Pt other_pos = other_widget->worldPos();
        const Pt other_size = other_widget->size();

        const Pt ref_center(ref_pos.x + ref_size.x * 0.5f, ref_pos.y + ref_size.y * 0.5f);
        const Pt other_center(other_pos.x + other_size.x * 0.5f,
                              other_pos.y + other_size.y * 0.5f);

        ref_node->setPortLayoutSide(editor_math::side_from_relative_position(ref_center, other_center));
    }
}

// ============================================================================
// Node widget creation
// ============================================================================

std::unique_ptr<Widget> create_node_widget(const bp2::Blueprint::Node& n,
                                            const bp2::Blueprint& bp,
                                            core::StringInterner& interner,
                                            bp2::PathArena& arena,
                                            std::span<const core::InternedId> instance_path,
                                            const ComponentRegistry& registry,
                                            const editor::RuntimeNodeStateStore* runtime_state_store,
                                            const editor::IconFont* icon_font,
                                            const std::vector<BusWireRef>& bus_wires) {
    const bp2::Interface render_iface = bp.resolve_node_iface(
        n,
        bp2::Blueprint::NodeIfaceAuthority{interner, &registry});
    const std::string type_name(interner.resolve(n.semantic.type));
    const ComponentSpec* def = registry.get(type_name);
    const TypePresentation* pres = registry.get_presentation(type_name);
    auto frame_kind = editor::presentation::resolve_frame_kind(def, pres);
    const editor::NodeInstanceKey instance_key = editor::make_node_instance_key(instance_path, n.semantic.id);
    const editor::RuntimeNodeState* runtime_state = nullptr;
    if (runtime_state_store != nullptr) {
        const auto it = runtime_state_store->find(instance_key);
        if (it != runtime_state_store->end()) {
            runtime_state = &it->second;
        }
    }
    NodeContent const content = def ? create_runtime_node_content(n, *def, pres, interner, runtime_state) : NodeContent{};
    editor::NodeBadgeSet badges;
    if (n.is_blueprint_instance()) {
        badges.set(editor::NodeBadge::Composite);
    }
    std::optional<editor::NodeColor> const color = n.view.color;
    return NodeFactory::create(
        n, frame_kind, render_iface, interner, content, badges, icon_font, color, bus_wires);
}

// ============================================================================
// Public API
// ============================================================================

/// Full scene rebuild from a Blueprint.
///
/// **Dual-path color contract (PULL path):**
/// Node color is read from `n.view.color` and passed to `NodeFactory::create()`.
/// The PUSH path (`dispatch_color_to_widget` in `document_simulation.cpp`) pushes
/// the same `node.view.color` directly to live widgets after mutation. Both paths
/// derive color via `NodeColor::to_uint32()` and must stay in sync.
void rebuild(Scene& scene,
              const bp2::Blueprint& bp,
              core::StringInterner& interner,
              bp2::PathArena& arena,
              std::span<const core::InternedId> instance_path,
              const ComponentRegistry& registry,
              const editor::RuntimeNodeStateStore* runtime_state_store,
              const editor::IconFont* icon_font) {
    auto guard = scene.flushGuard();
    scene.clear();

    const std::vector<BusWireRef> bus_wires = build_bus_wires(bp, arena);

    // 1) Create node widgets for all nodes in this group
    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        auto widget = create_node_widget(n, bp, interner, arena, instance_path, registry,
                                          runtime_state_store, icon_font, bus_wires);
        scene.add(std::move(widget));
    }

    // Orient single-port ref/value nodes toward their connected node.
    orient_ref_node_ports(scene, bp, arena, interner, instance_path, registry);

    // 2) Create wire widgets for wires whose both endpoints are in this group
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node_id, src_port] = editor_math::path_to_node_port(w.source, arena);
        auto [tgt_node_id, tgt_port] = editor_math::path_to_node_port(w.target, arena);
        if (src_node_id.empty() || tgt_node_id.empty()) continue;

        const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
        const bp2::Blueprint::Node* en = bp.find_node(tgt_node_id);
        if (!sn || !en) continue;

        auto wire_widget = create_wire_widget(w, arena, interner, scene);
        if (wire_widget) scene.add(std::move(wire_widget));
    }
}

} // namespace visual::mutations
