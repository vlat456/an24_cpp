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
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"
#include "visual/snap.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <unordered_map>

namespace visual::mutations {

// side_from_relative_position() and path_to_node_port() live in editor_math (snap.h)

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

// ============================================================================
// Wire widget helpers
// ============================================================================

/// Resolve a node/port endpoint to a visual::Port* in the scene.
static Port* resolve_port(Scene& scene,
                          ui::InternedId node_id,
                          ui::InternedId port_name,
                          ui::InternedId wire_id,
                          const ui::StringInterner& interner) {
    std::string_view node_sv = interner.resolve(node_id);
    Widget* widget = scene.find(node_sv);
    if (!widget) return nullptr;
    std::string_view port_sv  = interner.resolve(port_name);
    std::string_view wire_sv  = interner.resolve(wire_id);
    return widget->portByName(port_sv, wire_sv);
}

/// Create a visual::Wire widget from a bp2::Blueprint::Wire and add it to the scene.
static visual::Wire* create_wire_widget(Scene& scene,
                                        const bp2::Blueprint::Wire& w,
                                        const bp2::PathArena& arena,
                                        const ui::StringInterner& interner) {
    auto [src_node_id, src_port] = editor_math::path_to_node_port(w.source, arena);
    auto [tgt_node_id, tgt_port] = editor_math::path_to_node_port(w.target, arena);
    if (src_node_id.empty() || src_port.empty() || tgt_node_id.empty() || tgt_port.empty()) {
        return nullptr;
    }

    Port* start_port = resolve_port(scene, src_node_id, src_port, w.id, interner);
    Port* end_port   = resolve_port(scene, tgt_node_id, tgt_port, w.id, interner);
    if (!start_port || !end_port) return nullptr;

    std::string_view wire_id_sv    = interner.resolve(w.id);
    std::string_view start_node_sv = interner.resolve(src_node_id);
    std::string_view start_port_sv = interner.resolve(src_port);
    std::string_view end_node_sv   = interner.resolve(tgt_node_id);
    std::string_view end_port_sv   = interner.resolve(tgt_port);

    auto wire_widget = std::make_unique<visual::Wire>(
        wire_id_sv,
        start_node_sv, start_port_sv,
        end_node_sv,   end_port_sv);
    visual::Wire* wire_ptr = wire_widget.get();

    for (size_t i = 0; i < w.routing_points.size(); ++i) {
        wire_widget->addRoutingPoint(ui::Pt(w.routing_points[i].first, w.routing_points[i].second), i);
    }

    scene.add(std::move(wire_widget));
    return wire_ptr;
}

// ============================================================================
// Ref/Value node port orientation
// ============================================================================

static void orient_ref_node_ports(Scene& scene,
                                  const bp2::Blueprint& bp,
                                  const bp2::PathArena& arena,
                                  const ui::StringInterner& interner,
                                  std::string_view scope_id,
                                  const TypeRegistry& registry) {
    using editor::presentation::NodeFrameKind;
    std::unordered_map<ui::InternedId, ui::InternedId> ref_to_connected;

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node_id, _src_port] = editor_math::path_to_node_port(w.source, arena);
        auto [tgt_node_id, _tgt_port] = editor_math::path_to_node_port(w.target, arena);
        if (src_node_id.empty() || tgt_node_id.empty()) continue;

         const bp2::Blueprint::Node* src_node = bp.find_node(src_node_id);
         const bp2::Blueprint::Node* tgt_node = bp.find_node(tgt_node_id);
         if (!src_node || !tgt_node) continue;

         auto src_kind = editor::presentation::resolve_frame_kind(
             registry.get(std::string(interner.resolve(src_node->semantic.type))));
         auto tgt_kind = editor::presentation::resolve_frame_kind(
             registry.get(std::string(interner.resolve(tgt_node->semantic.type))));

         if (src_kind == NodeFrameKind::Reference && ref_to_connected.count(src_node_id) == 0) {
             ref_to_connected.emplace(src_node_id, tgt_node_id);
         }
         if (tgt_kind == NodeFrameKind::Reference && ref_to_connected.count(tgt_node_id) == 0) {
             ref_to_connected.emplace(tgt_node_id, src_node_id);
         }
    }

    for (const auto& [ref_id, other_id] : ref_to_connected) {
        Widget* ref_widget = scene.find(interner.resolve(ref_id));
        Widget* other_widget = scene.find(interner.resolve(other_id));
        if (!ref_widget || !other_widget) continue;

        auto* ref_node = dynamic_cast<RefNodeWidget*>(ref_widget);
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
// Public API
// ============================================================================

void rebuild(Scene& scene,
             const bp2::Blueprint& bp,
             ui::StringInterner& interner,
             bp2::PathArena& arena,
             std::string_view scope_id,
             const TypeRegistry& registry) {
    auto guard = scene.flushGuard();
    scene.clear();

    // Build wire refs for BusNodeWidget alias port construction
    std::vector<BusWireRef> bus_wires;
    bus_wires.reserve(bp.wires().size());
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        std::optional<BusWireRef> bw = to_bus_wire_ref(w, arena);
        if (bw) bus_wires.push_back(*bw);
    }

     // 1) Create node widgets for all nodes in this group
     for (const bp2::Blueprint::Node& n : bp.nodes()) {
         const bp2::Interface& render_iface = bp.effective_node_iface(n);
         const std::string type_name(interner.resolve(n.semantic.type));
         const TypeDefinition* def = registry.get(type_name);
         auto frame_kind = editor::presentation::resolve_frame_kind(def);
         std::unique_ptr<Widget> widget = NodeFactory::create(n, frame_kind, render_iface, interner, bus_wires);
         scene.add(std::move(widget));
     }

    // Orient single-port ref/value nodes toward their connected node.
    orient_ref_node_ports(scene, bp, arena, interner, scope_id, registry);

    // 2) Create wire widgets for wires whose both endpoints are in this group
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node_id, src_port] = editor_math::path_to_node_port(w.source, arena);
        auto [tgt_node_id, tgt_port] = editor_math::path_to_node_port(w.target, arena);
        if (src_node_id.empty() || tgt_node_id.empty()) continue;

         const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
         const bp2::Blueprint::Node* en = bp.find_node(tgt_node_id);
         if (!sn || !en) continue;

        create_wire_widget(scene, w, arena, interner);
    }
}

} // namespace visual::mutations
