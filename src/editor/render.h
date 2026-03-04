#pragma once

#include "data/blueprint.h"
#include "viewport.h"
#include "data/pt.h"
#include <cstdint>

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
};

/// Рендерит Blueprint (узлы и провода) в IDrawList
void render_blueprint(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max);

/// Рендерит сетку
void render_grid(IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max);

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
    }
    void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) override {
        (void)center; (void)radius; (void)color; (void)segments;
    }
    void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        (void)pos; (void)text; (void)color; (void)font_size;
    }
    void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        (void)points; (void)count; (void)color; (void)thickness;
    }

    bool had_line() const { return had_line_; }
    bool had_rect() const { return had_rect_; }
    bool had_circle() const { return had_circle_; }
};

#endif // EDITOR_TESTING
