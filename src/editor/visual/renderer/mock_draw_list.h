#pragma once

#include "visual/renderer/draw_list.h"
#include <vector>

#ifdef EDITOR_TESTING

/// Mock DrawList for testing — records draw calls for assertions.
class MockDrawList : public IDrawList {
public:
    bool had_line_ = false;
    bool had_rect_ = false;
    bool had_circle_ = false;
    bool had_polyline_ = false;

    struct TextEntry {
        Pt pos;
        std::string text;
        uint32_t color;
    };
    std::vector<TextEntry> texts_;
    std::vector<uint32_t> polyline_colors_;
    std::vector<uint32_t> circle_colors_;

    void add_line(Pt, Pt, uint32_t, float) override { had_line_ = true; }
    void add_rect(Pt, Pt, uint32_t, float) override { had_rect_ = true; }
    void add_rect_filled(Pt, Pt, uint32_t) override {}
    void add_circle(Pt, float, uint32_t color, int) override {
        had_circle_ = true;
        circle_colors_.push_back(color);
    }
    void add_circle_filled(Pt, float, uint32_t color, int) override {
        had_circle_ = true;
        circle_colors_.push_back(color);
    }
    void add_text(Pt pos, const char* text, uint32_t color, float) override {
        texts_.push_back({pos, std::string(text), color});
    }
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(std::strlen(text) * font_size * 0.6f, font_size);
    }
    void add_polyline(const Pt*, size_t, uint32_t color, float) override {
        had_polyline_ = true;
        polyline_colors_.push_back(color);
    }

    bool had_line() const { return had_line_; }
    bool had_rect() const { return had_rect_; }
    bool had_circle() const { return had_circle_; }
    bool had_polyline() const { return had_polyline_; }

    bool has_polyline_with_color(uint32_t color) const {
        for (auto c : polyline_colors_) if (c == color) return true;
        return false;
    }
    bool has_circle_with_color(uint32_t color) const {
        for (auto c : circle_colors_) if (c == color) return true;
        return false;
    }
};

#endif // EDITOR_TESTING
