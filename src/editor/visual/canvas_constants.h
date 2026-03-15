#pragma once

#include <cstdint>


/// Canvas rendering constants - single source of truth
namespace CanvasConstants {
    /// Hover clear position (off-screen)
    constexpr float HOVER_CLEAR_X = -1e9f;
    constexpr float HOVER_CLEAR_Y = -1e9f;
    
    /// Scroll zoom multiplier
    constexpr float SCROLL_ZOOM_FACTOR = 10.0f;
    
    /// Epsilon for float comparisons
    constexpr float FLOAT_EPSILON = 1e-6f;
}

/// Canvas color constants (ABGR format for ImGui)
namespace CanvasColors {
    constexpr uint32_t TEMP_WIRE_NEW = 0xC8FFFF64;       // Yellow, semi-transparent
    constexpr uint32_t TEMP_WIRE_RECONNECT = 0xC8963AFF; // Orange, semi-transparent
    
    constexpr uint32_t MARQUEE_FILL = 0x4000FF00;        // Green fill, semi-transparent
    constexpr uint32_t MARQUEE_BORDER = 0xFF00FF00;      // Green border, opaque
}

