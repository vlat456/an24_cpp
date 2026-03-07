#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "data/pt.h"
#include "../jit_solver/simulator.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>

/// Абстрактный DrawList - для рендеринга
///imgui совместимый интерфейс
struct IDrawList {
    virtual ~IDrawList() = default;

    virtual void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect_filled(Pt min, Pt max, uint32_t color) = 0;
    virtual void add_circle(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) = 0;
    virtual void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) = 0;
    virtual Pt calc_text_size(const char* text, float font_size) const = 0;
};

/// Tooltip info for hovered elements (port/wire)
struct TooltipInfo {
    bool active = false;
    Pt screen_pos;           // screen position for tooltip
    std::string text;        // tooltip text (e.g., "28.3")
    std::string label;       // label (e.g., "bat.v_out")
};

// Forward declaration
class VisualNodeCache;

/// Рендерит Blueprint (узлы и провода) в IDrawList
void render_blueprint(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max,
                      VisualNodeCache& cache,
                      const std::vector<size_t>* selected_nodes = nullptr,
                      std::optional<size_t> selected_wire = std::nullopt,
                      const class an24::Simulator<an24::JIT_Solver>* simulation = nullptr,
                      const Pt* hover_world_pos = nullptr,
                      TooltipInfo* out_tooltip = nullptr);

/// Рендерит tooltip (вызывается после render_blueprint)
void render_tooltip(IDrawList* dl, const TooltipInfo& tooltip);

/// Рендерит сетку
void render_grid(IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max);

/// Get port color based on port type for visualization
/// @param type Port type to get color for
/// @return Color in ImGui format (0xAABBGGRR)
uint32_t get_port_color(an24::PortType type);

// =============================================================================
// Утилиты для тестов
// =============================================================================

#ifdef EDITOR_TESTING

/// Mock DrawList для тестирования
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

    void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) override {
        (void)a; (void)b; (void)color; (void)thickness;
        had_line_ = true;
    }
    void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) override {
        (void)min; (void)max; (void)color; (void)thickness;
        had_rect_ = true;
    }
    void add_rect_filled(Pt min, Pt max, uint32_t color) override {
        (void)min; (void)max; (void)color;
    }
    void add_circle(Pt center, float radius, uint32_t color, int segments = 12) override {
        (void)center; (void)radius; (void)color; (void)segments;
        had_circle_ = true;
        circle_colors_.push_back(color);
    }
    void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) override {
        (void)center; (void)radius; (void)color; (void)segments;
        had_circle_ = true;
        circle_colors_.push_back(color);
    }
    void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        (void)font_size;
        texts_.push_back({pos, std::string(text), color});
    }
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(strlen(text) * font_size * 0.6f, font_size);
    }
    void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        (void)points; (void)count; (void)thickness;
        had_polyline_ = true;
        polyline_colors_.push_back(color);
    }

    bool had_line() const { return had_line_; }
    bool had_rect() const { return had_rect_; }
    bool had_circle() const { return had_circle_; }
    bool had_polyline() const { return had_polyline_; }

    bool has_polyline_with_color(uint32_t color) const {
        for (auto c : polyline_colors_) {
            if (c == color) return true;
        }
        return false;
    }

    bool has_circle_with_color(uint32_t color) const {
        for (auto c : circle_colors_) {
            if (c == color) return true;
        }
        return false;
    }
};

#endif // EDITOR_TESTING
