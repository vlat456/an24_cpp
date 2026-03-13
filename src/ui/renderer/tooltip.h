#pragma once

/// Reusable tooltip rendering for IDrawList-based UIs.
///
/// Draws a compact "label: value" box near a screen position.
/// Framework-agnostic — only depends on ui::Pt and ui::IDrawList.

#include "ui/math/pt.h"
#include "ui/renderer/idraw_list.h"
#include <string>
#include <cstdint>

namespace ui {

// ============================================================
// Tooltip data
// ============================================================

struct Tooltip {
    bool active = false;
    Pt   screen_pos;       ///< Anchor point (tooltip appears above-right)
    std::string label;     ///< Left part  (dimmer)
    std::string value;     ///< Right part (bright)
};

// ============================================================
// Tooltip style (compile-time defaults, overrideable per call)
// ============================================================

struct TooltipStyle {
    float    font_size   = 14.0f;
    float    padding     = 4.0f;
    float    rounding    = 3.0f;
    uint32_t bg_color    = 0xCC1A1A1A;   ///< ABGR dark semi-transparent
    uint32_t label_color = 0xFFAAAAAA;   ///< ABGR grey
    uint32_t value_color = 0xFFFFFFFF;   ///< ABGR white
};

// ============================================================
// Rendering
// ============================================================

/// Draw a tooltip. No-op if !tooltip.active.
inline void render_tooltip(IDrawList& dl, const Tooltip& tip,
                           const TooltipStyle& style = {}) {
    if (!tip.active) return;

    const std::string label_part = tip.label + ": ";
    const std::string full_text  = label_part + tip.value;

    Pt text_size = dl.calc_text_size(full_text.c_str(), style.font_size);

    // Position the box above-right of the anchor
    Pt bg_min(tip.screen_pos.x,
              tip.screen_pos.y - text_size.y - style.padding * 2);
    Pt bg_max(tip.screen_pos.x + text_size.x + style.padding * 2,
              tip.screen_pos.y);

    dl.add_rect_filled_with_rounding(bg_min, bg_max, style.bg_color, style.rounding);

    Pt text_origin(bg_min.x + style.padding, bg_min.y + style.padding);

    // Label (dim)
    dl.add_text(text_origin, label_part.c_str(), style.label_color, style.font_size);

    // Value (bright), offset by label width
    Pt label_size = dl.calc_text_size(label_part.c_str(), style.font_size);
    Pt value_origin(text_origin.x + label_size.x, text_origin.y);
    dl.add_text(value_origin, tip.value.c_str(), style.value_color, style.font_size);
}

} // namespace ui
