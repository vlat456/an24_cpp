#pragma once

#include "ui/math/pt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ui {

/// Abstract draw list — imgui-compatible rendering interface.
struct IDrawList {
    virtual ~IDrawList() = default;

    virtual void set_clip_rect(Pt min, Pt max) {}
    virtual void clear_clip() {}

    virtual void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners, float thickness = 1.0f) = 0;
    virtual void add_rect_filled(Pt min, Pt max, uint32_t color) = 0;
    virtual void add_rect_filled_with_rounding(Pt min, Pt max, uint32_t color, float rounding) = 0;
    virtual void add_rect_filled_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners) = 0;
    virtual void add_circle(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) = 0;
    virtual void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_triangle_filled(Pt a, Pt b, Pt c, uint32_t color) = 0;
    virtual Pt calc_text_size(const char* text, float font_size) const = 0;

    /// Opaque native texture handle. Zero = no texture.
    /// Backends store their platform-specific texture ID here (GL GLuint,
    /// Vulkan VkImage, etc). Value is just an integer — never dereferenced.
    using NativeTexture = uintptr_t;

    /// Blit a pre-rendered texture quad (for sprite cache / port circles).
    /// `tex` is an opaque native texture handle. Zero = no texture.
    /// UV coordinates default to (0,0)→(1,1). FBO textures need Y-flip:
    /// pass uv_min=(0,1), uv_max=(1,0).
    /// Default falls back to a filled rect (sufficient for test builds).
    virtual void add_image(NativeTexture tex, Pt min, Pt max,
                           Pt uv_min = Pt(0, 0), Pt uv_max = Pt(1, 1),
                           uint32_t color = 0xFFFFFFFF) {
        (void)tex;
        (void)uv_min;
        (void)uv_max;
        add_rect_filled(min, max, color);
    }

    /// Access the underlying platform draw list for hot-path bypass.
    /// Returns nullptr if no native draw list is available.
    virtual void* native_draw_list() const { return nullptr; }

    /// Render text with an explicit font handle (opaque, platform-specific).
    /// Default falls through to add_text (ignores font_handle).
    /// Used for icon font rendering where per-icon color control is needed.
    virtual void add_text_with_font(Pt pos, const char* text, uint32_t color,
                                     float font_size, const void* font_handle) {
        (void)font_handle;
        add_text(pos, text, color, font_size);
    }

    /// Measure text with an explicit font handle.
    /// Default falls through to calc_text_size (ignores font_handle).
    virtual Pt calc_text_size_with_font(const char* text, float font_size,
                                          const void* font_handle) const {
        (void)font_handle;
        return calc_text_size(text, font_size);
    }
};

} // namespace ui
