#pragma once

#include "ui/renderer/idraw_list.h"
#include "ui/math/pt.h"
#include <imgui.h>

using ui::IDrawList;

/// ImGui adapter for IDrawList interface
/// Wraps ImDrawList* from ImGui for use with the blueprint renderer
class ImGuiDrawList : public IDrawList {
public:
    ImDrawList* dl = nullptr;

    void set_clip_rect(Pt min, Pt max) {
        ImGui::PushClipRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), true);
    }

    void clear_clip() {
        ImGui::PopClipRect();
    }

    void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), c, thickness);
    }

    void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, 0, 0, thickness);
    }

    void add_rect_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, corners, thickness);
    }

    void add_rect_filled(Pt min, Pt max, uint32_t color) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c);
    }

    void add_rect_filled_with_rounding(Pt min, Pt max, uint32_t color, float rounding) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, ImDrawFlags_RoundCornersAll);
    }

    void add_rect_filled_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, rounding, corners);
    }

    void add_circle(Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircle(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircleFilled(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, font_size, ImVec2(pos.x, pos.y), c, text);
    }

    Pt calc_text_size(const char* text, float font_size) const override {
        ImFont* font = ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return Pt(size.x, size.y);
    }

    void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        if (count < 2) return;
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        // Pt and ImVec2 are both {float x, float y} — layout-compatible.
        static_assert(sizeof(Pt) == sizeof(ImVec2), "Pt and ImVec2 must have same layout");
        const auto* im_pts = reinterpret_cast<const ImVec2*>(points);
        dl->AddPolyline(im_pts, (int)count, c, false, thickness);
    }
};
