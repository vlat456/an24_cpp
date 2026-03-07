#pragma once

#include "data/pt.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include "json_parser/json_parser.h"

struct IDrawList;

// Bounding rectangle in local coordinates
struct Bounds {
    float x = 0, y = 0, w = 0, h = 0;

    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// ============================================================================
// Widget - Abstract base class for all layout elements
// ============================================================================
// Coordinates are in local node space (world units, origin = node top-left).
// During render(), transformed to screen space via origin + zoom.

class Widget {
public:
    virtual ~Widget() = default;

    // Position relative to parent
    float x() const { return x_; }
    float y() const { return y_; }
    float width() const { return width_; }
    float height() const { return height_; }

    void setPosition(float x, float y) { x_ = x; y_ = y; }
    void setSize(float w, float h) { width_ = w; height_ = h; }

    Pt getSize() const { return Pt(width_, height_); }
    Bounds getBounds() const { return {x_, y_, width_, height_}; }

    // Natural/preferred size. Override for measured content.
    virtual Pt getPreferredSize(IDrawList* dl) const;

    // Lay out children within available space.
    virtual void layout(float available_width, float available_height);

    // Render at screen origin with zoom.
    // origin = screen position of this widget's top-left.
    virtual void render(IDrawList* dl, Pt origin, float zoom) const = 0;

    // Flexible widgets expand to fill remaining space in layouts.
    bool isFlexible() const { return flexible_; }
    void setFlexible(bool f) { flexible_ = f; }

protected:
    float x_ = 0, y_ = 0;
    float width_ = 0, height_ = 0;
    bool flexible_ = false;
};

// ============================================================================
// ColumnLayout - Stacks children vertically
// ============================================================================
// Fixed children get their preferred height.
// Flexible children share remaining vertical space equally.

class ColumnLayout : public Widget {
public:
    Widget* addWidget(std::unique_ptr<Widget> child);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    size_t childCount() const { return children_.size(); }
    Widget* child(size_t i) const;

private:
    std::vector<std::unique_ptr<Widget>> children_;
};

// ============================================================================
// HeaderWidget - Node name in a colored header bar
// ============================================================================

class HeaderWidget : public Widget {
public:
    HeaderWidget(const std::string& name, uint32_t fill_color);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float HEIGHT = 24.0f; // 20 visual + 4 spacing

private:
    std::string name_;
    uint32_t fill_color_;
    static constexpr float VISUAL_HEIGHT = 20.0f;
    static constexpr float FONT_SIZE = 12.0f;
    static constexpr float PADDING = 5.0f;
};

// ============================================================================
// TypeNameWidget - Type label centered at bottom
// ============================================================================

class TypeNameWidget : public Widget {
public:
    explicit TypeNameWidget(const std::string& type_name);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float HEIGHT = 16.0f;

private:
    std::string type_name_;
    static constexpr float FONT_SIZE = 10.0f;
};

// ============================================================================
// PortRowWidget - One row: circle-label-[space]-label-circle
// ============================================================================
// Port circles centered at node edges: left at x=0, right at x=node_width.
// Labels adjacent to circles on interior side.
// Free space in the middle for content.

class PortRowWidget : public Widget {
public:
    PortRowWidget(const std::string& left_port = "",
                  const std::string& right_port = "",
                  an24::PortType left_type = an24::PortType::Any,
                  an24::PortType right_type = an24::PortType::Any);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    const std::string& leftPortName() const { return left_port_; }
    const std::string& rightPortName() const { return right_port_; }

    // Port circle center in local node coordinates
    Pt leftPortCenter() const;
    Pt rightPortCenter() const;

    // Content area bounds (local coords, for future ImGui content)
    Bounds contentBounds() const { return content_bounds_; }

    static constexpr float PORT_RADIUS = 6.0f;
    static constexpr float LABEL_GAP = 3.0f;
    static constexpr float LABEL_FONT_SIZE = 9.0f;
    static constexpr float ROW_HEIGHT = 16.0f;

private:
    std::string left_port_;
    std::string right_port_;
    an24::PortType left_type_ = an24::PortType::Any;
    an24::PortType right_type_ = an24::PortType::Any;
    float left_label_width_ = 0;
    float right_label_width_ = 0;
    Bounds content_bounds_ = {};

    static float estimateLabelWidth(const std::string& label);

    static constexpr uint32_t COLOR_PORT_INPUT = 0xFFDCDCB4;
    static constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;
    static constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;
};

// ============================================================================
// ContentWidget - Flexible content placeholder (gauge, switch, value)
// ============================================================================

class ContentWidget : public Widget {
public:
    ContentWidget(const std::string& label = "", float value = 0.0f,
                  float left_margin = 0.0f, float right_margin = 0.0f);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    // Get the actual content area bounds (excluding margins)
    Bounds getContentArea() const {
        return {
            left_margin_,
            0,
            std::max(0.0f, width_ - left_margin_ - right_margin_),
            height_
        };
    }

private:
    std::string label_;
    float value_;
    float left_margin_;
    float right_margin_;
    static constexpr float FONT_SIZE = 10.0f;
    static constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;
    static constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;
};

// ============================================================================
// VoltmeterWidget - Visual steam gauge (analog voltmeter)
// ============================================================================

class VoltmeterWidget : public Widget {
public:
    VoltmeterWidget(float value = 0.0f, float min_val = 0.0f, float max_val = 30.0f,
                   const std::string& unit = "V");

    void setValue(float value) { value_ = value; }
    float getValue() const { return value_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float GAUGE_RADIUS = 40.0f;
    static constexpr float NEEDLE_LENGTH = 32.0f;

private:
    mutable float value_;  // mutable to allow updates in const render()
    float min_val_;
    float max_val_;
    std::string unit_;

    // Gauge angles in degrees (standard math: 0° = right, CCW positive).
    // Rendered with Y-flip (cy - sin) so CW on screen = negative sweep.
    // Start at bottom-left (210°), sweep CW 240° to bottom-right (330°).
    static constexpr float START_ANGLE = 210.0f;
    static constexpr float SWEEP_ANGLE = -240.0f;  // negative = CW on screen

    static constexpr uint32_t COLOR_GAUGE_BG = 0xFF2A2A2A;
    static constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF4A4A4A;
    static constexpr uint32_t COLOR_NEEDLE = 0xFFFF4444;
    static constexpr uint32_t COLOR_TICK_MAJOR = 0xFFFFFFFF;
    static constexpr uint32_t COLOR_TICK_MINOR = 0xFF888888;
    static constexpr uint32_t COLOR_TEXT = 0xFFCCCCCC;
    static constexpr float VALUE_FONT_SIZE = 14.0f;
    static constexpr float UNIT_FONT_SIZE = 10.0f;
};
