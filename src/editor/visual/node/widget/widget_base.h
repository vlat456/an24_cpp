#pragma once

#include "data/pt.h"
#include "visual/node/bounds.h"

struct IDrawList;
struct NodeContent;

class Widget {
public:
    virtual ~Widget() = default;

    float x() const { return x_; }
    float y() const { return y_; }
    float width() const { return width_; }
    float height() const { return height_; }

    void setPosition(float x, float y) { x_ = x; y_ = y; }
    void setSize(float w, float h) { width_ = w; height_ = h; }

    Pt getSize() const { return Pt(width_, height_); }
    Bounds getBounds() const { return {x_, y_, width_, height_}; }

    virtual Pt getPreferredSize(IDrawList* dl) const;
    virtual void layout(float available_width, float available_height);
    virtual void render(IDrawList* dl, Pt origin, float zoom) const = 0;

    virtual void updateFromContent(const NodeContent& content);

    bool isFlexible() const { return flexible_; }
    void setFlexible(bool f) { flexible_ = f; }

protected:
    float x_ = 0, y_ = 0;
    float width_ = 0, height_ = 0;
    bool flexible_ = false;
};
