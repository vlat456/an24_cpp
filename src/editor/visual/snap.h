#pragma once

#include "ui/math/pt.h"
#include "core/strings/interned_id.h"
#include "blueprint_v2/blueprint/node_port.h"
#include "editor/layout_constants.h"
#include "editor/visual/presentation/node_presentation.h"
#include "blueprint_v2/path/path.h"
#include <cmath>

namespace editor_math {

using ui::Pt;

// =====================================================================
// Path helpers
// =====================================================================

/// Extract (node_id, port_name) InternedId pair from a bp2 wire endpoint Path.
/// A port path has kind=Port with segment=port_name; its parent has segment=node_id.
/// Returns {empty, empty} if the path is malformed.
inline std::pair<core::InternedId, core::InternedId>
path_to_node_port(const bp2::Path& path, const bp2::PathArena& arena) {
    if (path.kind() != bp2::PathKind::Port) {
        return {};
    }
    core::InternedId port_name = path.segment();
    bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) {
        return {};
    }
    return {parent.segment(), port_name};
}

/// Overload for WireEndpoint — trivially extracts node/port without arena.
inline std::pair<core::InternedId, core::InternedId>
path_to_node_port(const bp2::WireEndpoint& ep, const bp2::PathArena& /*arena*/) {
    return {ep.node, ep.port};
}

// =====================================================================
// Snap granularity — maps NodeFrameKind → grid step multiplier.
// Single authoritative source for "how fine should snapping be?"
// =====================================================================

/// Snap granularity multiplier for a given node frame kind.
///   Reference → 0.5 (half-grid: between grid lines)
///   All others → 1.0 (whole-grid: on grid lines)
constexpr float snap_granularity(editor::presentation::NodeFrameKind kind) {
    return kind == editor::presentation::NodeFrameKind::Reference ? 0.5f : 1.0f;
}

// =====================================================================
// User-facing grid snap (input / placement)
// Zoom-dependent via grid_step parameter.
// =====================================================================

/// Snap a position to the user-facing grid.
/// @param pos          World-space position to snap.
/// @param grid_step    Current zoom-dependent grid step.
/// @param granularity  Step multiplier (1.0 = whole grid, 0.5 = half grid).
///                     Use snap_granularity(frame_kind) for type-aware snap.
inline Pt snap_to_grid(Pt pos, float grid_step, float granularity = 1.0f) {
    if (grid_step < 1e-6f) return pos;
    float step = grid_step * granularity;
    return Pt(
        std::round(pos.x / step) * step,
        std::round(pos.y / step) * step
    );
}

// =====================================================================
// Internal layout grid (port positioning, size quantization)
// Fixed 16px step — zoom-independent.
// =====================================================================

/// Snap a position to the internal port-layout grid (round to nearest).
inline Pt snap_to_layout_grid(Pt pos) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return Pt(
        std::round(pos.x / g) * g,
        std::round(pos.y / g) * g
    );
}

/// Snap a size to the internal port-layout grid (round up to nearest).
inline Pt snap_size_to_layout_grid(Pt size) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return Pt(
        std::ceil(size.x / g) * g,
        std::ceil(size.y / g) * g
    );
}

/// Snap a scalar size to the internal port-layout grid (round up to nearest).
inline float snap_size_to_layout_grid(float v) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return std::ceil(v / g) * g;
}

// =====================================================================
// Geometry helpers
// =====================================================================

/// Given two center positions, determine which edge of `from_center` faces
/// `to_center` (the dominant axis wins; ties go to horizontal).
inline bp2::PortLayoutSide side_from_relative_position(Pt from_center, Pt to_center) {
    const float dx = to_center.x - from_center.x;
    const float dy = to_center.y - from_center.y;
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0f) ? bp2::PortLayoutSide::Right : bp2::PortLayoutSide::Left;
    }
    return (dy >= 0.0f) ? bp2::PortLayoutSide::Bottom : bp2::PortLayoutSide::Top;
}

} // namespace editor_math
