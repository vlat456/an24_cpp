#pragma once

#include "ui/renderer/idraw_list.h"
#include "ui/math/pt.h"
#include <imgui.h>

/// ImGui wrapper for IDrawList interface.
/// Converts from internal ABGR uint32_t colors to ImGui's RGBA ImU32 format.
class ImGuiDrawList : public ui::IDrawList {
public:
    ImDrawList* dl = nullptr;

    void* native_draw_list() const override { return dl; }

    void set_clip_rect(ui::Pt min, ui::Pt max) override {
        ImGui::PushClipRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), true);
    }

    void clear_clip() override {
        ImGui::PopClipRect();
    }

    void add_line(ui::Pt a, ui::Pt b, uint32_t color, float thickness = 1.0f) override {
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), to_im_color(color), thickness);
    }

    void add_rect(ui::Pt min, ui::Pt max, uint32_t color, float thickness = 1.0f) override {
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), to_im_color(color), 0, 0, thickness);
    }

    void add_rect_with_rounding_corners(ui::Pt min, ui::Pt max, uint32_t color, float rounding, int corners, float thickness = 1.0f) override {
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), to_im_color(color), rounding, corners, thickness);
    }

    void add_rect_filled(ui::Pt min, ui::Pt max, uint32_t color) override {
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), to_im_color(color));
    }

    void add_rect_filled_with_rounding(ui::Pt min, ui::Pt max, uint32_t color, float rounding) override {
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), to_im_color(color), rounding, ImDrawFlags_RoundCornersAll);
    }

    void add_rect_filled_with_rounding_corners(ui::Pt min, ui::Pt max, uint32_t color, float rounding, int corners) override {
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), to_im_color(color), rounding, corners);
    }

    void add_circle(ui::Pt center, float radius, uint32_t color, int segments = 12) override {
        dl->AddCircle(ImVec2(center.x, center.y), radius, to_im_color(color), segments);
    }

    void add_circle_filled(ui::Pt center, float radius, uint32_t color, int segments = 12) override {
        dl->AddCircleFilled(ImVec2(center.x, center.y), radius, to_im_color(color), segments);
    }

    void add_text(ui::Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        dl->AddText(ImGui::GetFont(), font_size, ImVec2(pos.x, pos.y), to_im_color(color), text);
    }

    ui::Pt calc_text_size(const char* text, float font_size) const override {
        ImVec2 size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return ui::Pt(size.x, size.y);
    }

    void add_text_with_font(ui::Pt pos, const char* text, uint32_t color,
                             float font_size, ui::IDrawList::NativeFont font) override {
        ImFont* im_font = font
            ? reinterpret_cast<ImFont*>(font)
            : ImGui::GetFont();
        dl->AddText(im_font, font_size, ImVec2(pos.x, pos.y), to_im_color(color), text);
    }

    ui::Pt calc_text_size_with_font(const char* text, float font_size,
                                      ui::IDrawList::NativeFont font) const override {
        ImFont* im_font = font
            ? reinterpret_cast<ImFont*>(font)
            : ImGui::GetFont();
        ImVec2 size = im_font->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return ui::Pt(size.x, size.y);
    }

    void add_polyline(const ui::Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        if (count < 2) return;
        // ui::Pt and ImVec2 are both {float x, float y} — layout-compatible.
        static_assert(sizeof(ui::Pt) == sizeof(ImVec2), "ui::Pt and ImVec2 must have same layout");
        const auto* im_pts = reinterpret_cast<const ImVec2*>(points);
        dl->AddPolyline(im_pts, static_cast<int>(count), to_im_color(color), false, thickness);
    }

    void add_triangle_filled(ui::Pt a, ui::Pt b, ui::Pt c, uint32_t color) override {
        dl->AddTriangleFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), to_im_color(color));
    }

    void add_image(ui::IDrawList::NativeTexture tex, ui::Pt min, ui::Pt max,
                   ui::Pt uv_min = ui::Pt(0, 0), ui::Pt uv_max = ui::Pt(1, 1),
                   uint32_t color = 0xFFFFFFFF) override {
        auto tex_id = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex));
        dl->AddImage(tex_id,
                     ImVec2(min.x, min.y), ImVec2(max.x, max.y),
                     ImVec2(uv_min.x, uv_min.y), ImVec2(uv_max.x, uv_max.y),
                     to_im_color(color));
    }

private:
    /// Convert internal ABGR uint32_t to ImGui RGBA ImU32.
    /// Internal layout: 0xAABBGGRR (byte 0=R, byte 1=G, byte 2=B, byte 3=A).
    /// ImGui layout:     0xRRGGBBAA (standard RGBA).
    static ImU32 to_im_color(uint32_t abgr) {
        uint8_t r = static_cast<uint8_t>((abgr >>  0) & 0xFF);
        uint8_t g = static_cast<uint8_t>((abgr >>  8) & 0xFF);
        uint8_t b = static_cast<uint8_t>((abgr >> 16) & 0xFF);
        uint8_t a = static_cast<uint8_t>((abgr >> 24) & 0xFF);
        return IM_COL32(r, g, b, a);
    }
};
