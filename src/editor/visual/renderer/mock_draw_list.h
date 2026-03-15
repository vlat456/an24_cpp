#pragma once

#include "visual/renderer/draw_list.h"
#include <vector>

#ifdef EDITOR_TESTING

/// Mock DrawList for testing — records draw calls for assertions.
class MockDrawList : public IDrawList {
public:
    bool had_line_ = false;
    bool had_rect_ = false;
    size_t rect_count_ = 0;
    bool had_circle_ = false;
    bool had_polyline_ = false;

    struct TextEntry {
        Pt pos;
        std::string text;
        uint32_t color;
        float font_size;
    };
    struct RectFilledEntry {
        Pt min;
        Pt max;
        uint32_t color;
    };
    std::vector<TextEntry> texts_;
    std::vector<uint32_t> polyline_colors_;
    std::vector<uint32_t> circle_colors_;
    std::vector<uint32_t> rect_filled_colors_;
    std::vector<RectFilledEntry> rect_filled_entries_;
    std::vector<uint32_t> rect_border_colors_;

    struct CircleEntry {
        Pt center;
        float radius;
        uint32_t color;
        bool filled;
    };
    std::vector<CircleEntry> circle_entries_;

    void add_line(Pt, Pt, uint32_t, float) override { had_line_ = true; }
    void add_rect(Pt, Pt, uint32_t color, float) override { had_rect_ = true; rect_count_++; rect_border_colors_.push_back(color); }
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t color, float, int, float = 1.0f) override { had_rect_ = true; rect_count_++; rect_border_colors_.push_back(color); }
    void add_rect_filled(Pt min, Pt max, uint32_t color) override {
        had_rect_ = true;
        rect_count_++;
        rect_filled_colors_.push_back(color);
        rect_filled_entries_.push_back({min, max, color});
    }
    void add_rect_filled_with_rounding(Pt min, Pt max, uint32_t color, float) override {
        had_rect_ = true;
        rect_count_++;
        rect_filled_colors_.push_back(color);
        rect_filled_entries_.push_back({min, max, color});
    }
    void add_rect_filled_with_rounding_corners(Pt min, Pt max, uint32_t color, float, int) override {
        had_rect_ = true;
        rect_count_++;
        rect_filled_colors_.push_back(color);
        rect_filled_entries_.push_back({min, max, color});
    }
    void add_circle(Pt center, float radius, uint32_t color, int) override {
        had_circle_ = true;
        circle_colors_.push_back(color);
        circle_entries_.push_back({center, radius, color, false});
    }
    void add_circle_filled(Pt center, float radius, uint32_t color, int) override {
        had_circle_ = true;
        circle_colors_.push_back(color);
        circle_entries_.push_back({center, radius, color, true});
    }
    void add_text(Pt pos, const char* text, uint32_t color, float font_size) override {
        texts_.push_back({pos, std::string(text), color, font_size});
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
    size_t rect_count() const { return rect_count_; }
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
    bool has_rect_filled_with_color(uint32_t color) const {
        for (auto c : rect_filled_colors_) if (c == color) return true;
        return false;
    }
    bool has_rect_border_with_color(uint32_t color) const {
        for (auto c : rect_border_colors_) if (c == color) return true;
        return false;
    }
    bool has_filled_circle_with_color(uint32_t color) const {
        for (const auto& e : circle_entries_) if (e.filled && e.color == color) return true;
        return false;
    }
    bool has_outline_circle_with_color(uint32_t color) const {
        for (const auto& e : circle_entries_) if (!e.filled && e.color == color) return true;
        return false;
    }
    size_t count_circles_with_color(uint32_t color) const {
        size_t n = 0;
        for (auto c : circle_colors_) if (c == color) ++n;
        return n;
    }
};

#endif // EDITOR_TESTING
