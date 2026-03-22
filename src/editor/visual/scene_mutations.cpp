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
#include "wire/wire.h"
#include "wire/routing_point.h"
#include "data/node.h"
#include "data/wire.h"
#include "data/port.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace visual::mutations {

// ============================================================================
// Helpers: bp2 → legacy data conversion (for NodeFactory / WireWidget)
// ============================================================================

/// Convert a bp2::Blueprint::Node into the legacy ::Node format understood
/// by NodeFactory::create().  Only the fields required by the visual layer
/// are populated; simulation-only fields are left at defaults.
static ::Node to_legacy_node(const bp2::Blueprint::Node& n,
                              const ui::StringInterner& interner) {
    ::Node legacy;
    legacy.id          = n.id;
    legacy.name        = n.name;
    legacy.type_name   = std::string(interner.resolve(n.type));
    legacy.render_hint = n.render_hint;
    legacy.expandable  = n.expandable;
    legacy.collapsed   = n.collapsed;
    legacy.blueprint_path = n.blueprint_path;
    legacy.group_id    = n.group_id;
    legacy.pos         = ui::Pt(n.x, n.y);

    if (n.width.has_value() && n.height.has_value()) {
        legacy.set_explicit_size(ui::Pt(*n.width, *n.height));
    }

    // Ports
    legacy.inputs  = n.inputs;
    legacy.outputs = n.outputs;

    // Layout overrides
    for (const bp2::Blueprint::Node::PortLayoutOverride& ov : n.layout_overrides) {
        PortLayoutOverride lo;
        lo.port_name = ov.port_name;
        if (ov.side.has_value()) {
            lo.side = parse_port_layout_side(*ov.side);
        }
        if (ov.position.has_value()) {
            lo.position = static_cast<uint8_t>(*ov.position);
        }
        legacy.layout_overrides.push_back(std::move(lo));
    }

    // NodeContent
    legacy.node_content.type = static_cast<NodeContentType>(n.content_type);
    legacy.node_content.label  = n.content_label;
    legacy.node_content.value  = n.content_value;
    legacy.node_content.min    = n.content_min;
    legacy.node_content.max    = n.content_max;
    legacy.node_content.unit   = n.content_unit;
    legacy.node_content.state  = n.content_state;
    legacy.node_content.tripped = n.content_tripped;

    // Custom color
    if (n.has_color) {
        NodeColor col;
        col.r = n.color_r;
        col.g = n.color_g;
        col.b = n.color_b;
        col.a = n.color_a;
        legacy.color = col;
    }

    return legacy;
}

/// Extract (node_id, port_name) InternedId pair from a bp2 wire endpoint Path.
/// A port path has kind=Port with segment=port_name; its parent has segment=node_id.
/// Returns {empty, empty} if the path is malformed.
static std::pair<ui::InternedId, ui::InternedId>
path_to_node_port(const bp2::Path& path, const bp2::PathArena& arena) {
    if (path.kind() != bp2::PathKind::Port) {
        return {};
    }
    ui::InternedId port_name = path.segment();
    bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) {
        return {};
    }
    ui::InternedId node_id = parent.segment();
    return {node_id, port_name};
}

/// Build a legacy ::Wire from a bp2::Blueprint::Wire.
/// Returns std::nullopt if the source/target paths are malformed.
static std::optional<::Wire> to_legacy_wire(const bp2::Blueprint::Wire& w,
                                             const bp2::PathArena& arena) {
    auto [src_node, src_port] = path_to_node_port(w.source, arena);
    auto [tgt_node, tgt_port] = path_to_node_port(w.target, arena);
    if (src_node.empty() || src_port.empty() || tgt_node.empty() || tgt_port.empty()) {
        return std::nullopt;
    }

    ::Wire lw;
    lw.id    = w.id;
    lw.start = WireEnd(src_node, src_port, PortSide::Output);
    lw.end   = WireEnd(tgt_node, tgt_port, PortSide::Input);

    // Routing points: bp2 stores (float,float) pairs; legacy stores ui::Pt
    lw.routing_points.reserve(w.routing_points.size());
    for (const auto& [rx, ry] : w.routing_points) {
        lw.routing_points.push_back(ui::Pt(rx, ry));
    }

    return lw;
}

// ============================================================================
// Wire widget helpers
// ============================================================================

/// Resolve a WireEnd (legacy data) to a visual::Port* in the scene.
static Port* resolve_port(Scene& scene, const ::WireEnd& we,
                           const ui::StringInterner& interner,
                           ui::InternedId wire_id) {
    std::string_view node_sv = interner.resolve(we.node_id);
    Widget* widget = scene.find(node_sv);
    if (!widget) return nullptr;
    std::string_view port_sv  = interner.resolve(we.port_name);
    std::string_view wire_sv  = interner.resolve(wire_id);
    return widget->portByName(port_sv, wire_sv);
}

/// Create a visual::Wire widget from a legacy ::Wire and add it to the scene.
static visual::Wire* create_wire_widget(Scene& scene, const ::Wire& lw,
                                         const ui::StringInterner& interner) {
    Port* start_port = resolve_port(scene, lw.start, interner, lw.id);
    Port* end_port   = resolve_port(scene, lw.end,   interner, lw.id);
    if (!start_port || !end_port) return nullptr;

    std::string_view wire_id_sv    = interner.resolve(lw.id);
    std::string_view start_node_sv = interner.resolve(lw.start.node_id);
    std::string_view start_port_sv = interner.resolve(lw.start.port_name);
    std::string_view end_node_sv   = interner.resolve(lw.end.node_id);
    std::string_view end_port_sv   = interner.resolve(lw.end.port_name);

    auto wire_widget = std::make_unique<visual::Wire>(
        wire_id_sv,
        start_node_sv, start_port_sv,
        end_node_sv,   end_port_sv);
    visual::Wire* wire_ptr = wire_widget.get();

    for (size_t i = 0; i < lw.routing_points.size(); ++i) {
        wire_widget->addRoutingPoint(lw.routing_points[i], i);
    }

    scene.add(std::move(wire_widget));
    return wire_ptr;
}

// ============================================================================
// Public API
// ============================================================================

void rebuild(Scene& scene,
             const bp2::Blueprint& bp,
             ui::StringInterner& interner,
             bp2::PathArena& arena,
             std::string_view group_id) {
    auto guard = scene.flushGuard();
    scene.clear();

    // Build a flat list of legacy wires for BusNodeWidget construction
    std::vector<::Wire> legacy_wires;
    legacy_wires.reserve(bp.wires().size());
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        std::optional<::Wire> lw = to_legacy_wire(w, arena);
        if (lw) legacy_wires.push_back(std::move(*lw));
    }

    // 1) Create node widgets for all nodes in this group
    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        if (n.group_id != group_id) continue;
        ::Node legacy = to_legacy_node(n, interner);
        std::unique_ptr<Widget> widget = NodeFactory::create(legacy, interner, legacy_wires);
        scene.add(std::move(widget));
    }

    // 2) Create wire widgets for wires whose both endpoints are in this group
    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node_id, src_port] = path_to_node_port(w.source, arena);
        auto [tgt_node_id, tgt_port] = path_to_node_port(w.target, arena);
        if (src_node_id.empty() || tgt_node_id.empty()) continue;

        const bp2::Blueprint::Node* sn = bp.find_node(src_node_id);
        const bp2::Blueprint::Node* en = bp.find_node(tgt_node_id);
        if (!sn || !en) continue;
        if (sn->group_id != group_id || en->group_id != group_id) continue;

        std::optional<::Wire> lw = to_legacy_wire(w, arena);
        if (lw) create_wire_widget(scene, *lw, interner);
    }
}

} // namespace visual::mutations
