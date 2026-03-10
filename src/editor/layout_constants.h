#pragma once

#include <cstdint>

/// Central registry of grid and layout constants used across the editor.
/// [2.3] Eliminates magic numbers scattered in layout code.

namespace editor_constants {

// ---- Grid steps (user-facing, for snapping) ----
// Available grid steps are defined in viewport.cpp GRID_STEPS array.
// Default grid step for new blueprints/viewports:
constexpr float DEFAULT_GRID_STEP = 16.0f;

// ---- Internal layout grid (port/node layout) ----
// Used for snapping port positions and node sizes internally.
// NOT the same as user-facing grid_step.
constexpr float PORT_LAYOUT_GRID = 16.0f;

// ---- Auto-layout spacing ----
constexpr float LAYOUT_COL_SPACING = 200.0f;
constexpr float LAYOUT_ROW_SPACING = 120.0f;
constexpr float LAYOUT_ORIGIN_X    = 80.0f;
constexpr float LAYOUT_ORIGIN_Y    = 80.0f;

// ---- Port rendering ----
constexpr float PORT_RADIUS    = 4.0f;
constexpr float PORT_HIT_RADIUS = 10.0f;
constexpr float PORT_LABEL_GAP = 3.0f;
constexpr float PORT_LABEL_FONT_SIZE = 9.0f;
constexpr float PORT_ROW_HEIGHT = 16.0f;
constexpr uint32_t PORT_LABEL_COLOR = 0xFFAAAAAA;
constexpr float PORT_MIN_GAP = 20.0f;  // Minimum gap between left/right labels

// ---- Node sizing ----
constexpr float MIN_NODE_WIDTH = 80.0f;

// ---- Node rendering ----
constexpr float NODE_ROUNDING = 6.0f;  // Rounded corners for nodes

// ---- Draw corner flags (values match ImDrawFlags_ in imgui.h, stable across versions) ----
// ImDrawFlags_RoundCornersTopLeft=0x10, TopRight=0x20, BottomLeft=0x40, BottomRight=0x80
constexpr int DRAW_CORNERS_TOP    = 0x30;  // TopLeft | TopRight
constexpr int DRAW_CORNERS_BOTTOM = 0xC0;  // BottomLeft | BottomRight
constexpr int DRAW_CORNERS_ALL    = 0xF0;  // All four corners

// ---- Zoom bounds ----
constexpr float ZOOM_MIN   = 0.25f;
constexpr float ZOOM_MAX   = 4.0f;
constexpr float ZOOM_SPEED = 0.001f;

// ---- Hit test tolerances ----
constexpr float ROUTING_POINT_HIT_RADIUS = 10.0f;
constexpr float WIRE_SEGMENT_HIT_TOLERANCE = 5.0f;

// ---- Default collapsed group size ----
constexpr float COLLAPSED_GROUP_WIDTH  = 120.0f;
constexpr float COLLAPSED_GROUP_HEIGHT = 80.0f;

}  // namespace editor_constants
