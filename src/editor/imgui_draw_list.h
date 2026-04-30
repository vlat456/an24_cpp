#pragma once

#include "ui/renderer/idraw_list.h"
#include "ui/math/pt.h"
#include <imgui.h>

/// ImGui wrapper for IDrawList interface
/// Wraps ImDrawList* from ImGui for use with the blueprint renderer
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
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), c, thickness);
    }

    void add_rect(ui::Pt min, ui::Pt max, uint32_t color, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, 0, 0, thickness);
    }

    void add_rect_with_rounding_corners(ui::Pt min, ui::Pt max, uint32_t color, float rounding, int corners, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, corners, thickness);
    }

    void add_rect_filled(ui::Pt min, ui::Pt max, uint32_t color) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c);
    }

    void add_rect_filled_with_rounding(ui::Pt min, ui::Pt max, uint32_t color, float rounding) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, ImDrawFlags_RoundCornersAll);
    }

    void add_rect_filled_with_rounding_corners(ui::Pt min, ui::Pt max, uint32_t color, float rounding, int corners) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, corners);
    }

    void add_circle(ui::Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircle(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_circle_filled(ui::Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircleFilled(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_text(ui::Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, font_size, ImVec2(pos.x, pos.y), c, text);
    }

    ui::Pt calc_text_size(const char* text, float font_size) const override {
        ImFont* font = ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return ui::Pt(size.x, size.y);
    }

    void add_text_with_font(ui::Pt pos, const char* text, uint32_t color,
                             float font_size, const void* font_handle) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                            (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        ImFont* font = font_handle
            ? static_cast<ImFont*>(const_cast<void*>(font_handle))
            : ImGui::GetFont();
        dl->AddText(font, font_size, ImVec2(pos.x, pos.y), c, text);
    }

    ui::Pt calc_text_size_with_font(const char* text, float font_size,
                                      const void* font_handle) const override {
        ImFont* font = font_handle
            ? static_cast<ImFont*>(const_cast<void*>(font_handle))
            : ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return ui::Pt(size.x, size.y);
    }

    void add_polyline(const ui::Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        if (count < 2) return;
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        // ui::Pt and ImVec2 are both {float x, float y} — layout-compatible.
        static_assert(sizeof(ui::Pt) == sizeof(ImVec2), "ui::Pt and ImVec2 must have same layout");
        const auto* im_pts = reinterpret_cast<const ImVec2*>(points);
        dl->AddPolyline(im_pts, (int)count, c, false, thickness);
    }

    void add_triangle_filled(ui::Pt a, ui::Pt b, ui::Pt c, uint32_t color) override {
        ImU32 col = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                             (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddTriangleFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), col);
    }

    void add_image(ui::IDrawList::NativeTexture tex, ui::Pt min, ui::Pt max,
                   ui::Pt uv_min = ui::Pt(0, 0), ui::Pt uv_max = ui::Pt(1, 1),
                   uint32_t color = 0xFFFFFFFF) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                            (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        auto tex_id = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex));
        dl->AddImage(tex_id,
                     ImVec2(min.x, min.y), ImVec2(max.x, max.y),
                     ImVec2(uv_min.x, uv_min.y), ImVec2(uv_max.x, uv_max.y), c);
    }
};
