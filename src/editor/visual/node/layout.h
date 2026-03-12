#pragma once

#include "edges.h"
#include "widget_base.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <algorithm>

// ============================================================================
// Column — stacks children vertically
// ============================================================================
// Preferred: width = max(children), height = sum(children)
// Layout:    fixed children get preferred height,
//            flexible children share remaining space equally.
//            All children get full available width.

class Column : public Widget {
public:
    Widget* addChild(std::unique_ptr<Widget> child);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    size_t childCount() const { return children_.size(); }
    Widget* child(size_t i) const;

private:
    std::vector<std::unique_ptr<Widget>> children_;
};

// ============================================================================
// Row — lays out children horizontally
// ============================================================================
// Preferred: width = sum(children), height = max(children)
// Layout:    fixed children get preferred width,
//            flexible children share remaining space equally.
//            All children get full available height.

class Row : public Widget {
public:
    Widget* addChild(std::unique_ptr<Widget> child);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    size_t childCount() const { return children_.size(); }
    Widget* child(size_t i) const;

private:
    std::vector<std::unique_ptr<Widget>> children_;
};

// ============================================================================
// Container — single child with margins (positive or negative)
// ============================================================================
// Preferred: child.preferred + margins
// Layout:    child positioned at (left, top), sized to available - margins.
// Negative margins let child extend beyond container bounds (e.g. ports).

class Container : public Widget {
public:
    Container(std::unique_ptr<Widget> child, Edges margins);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    Widget* child() const { return child_.get(); }

private:
    std::unique_ptr<Widget> child_;
    Edges margins_;
};

// ============================================================================
// Label — self-sizing text widget
// ============================================================================

enum class TextAlign { Left, Right };

class Label : public Widget {
public:
    Label(const std::string& text, float font_size, uint32_t color = 0xFFFFFFFF,
          TextAlign align = TextAlign::Left);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

private:
    std::string text_;
    float font_size_;
    uint32_t color_;
    TextAlign align_;

    float estimateWidth() const;
};

// ============================================================================
// Circle — fixed-size filled circle (port indicator)
// ============================================================================

class Circle : public Widget {
public:
    Circle(float radius, uint32_t color);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

private:
    float radius_;
    uint32_t color_;
};

// ============================================================================
// Spacer — flexible empty space
// ============================================================================

class Spacer : public Widget {
public:
    Spacer();

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;
};
