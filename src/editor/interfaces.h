#pragma once

#include "data/pt.h"
#include <string>

struct IDrawList;
struct Viewport;
struct Wire;

// ============================================================================
// IDrawable — can render itself into a draw list
// ============================================================================

struct IDrawable {
    virtual ~IDrawable() = default;
    virtual void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                       bool is_selected) const = 0;
};

// ============================================================================
// ISelectable — can be tested for point containment (hit testing)
// ============================================================================

struct ISelectable {
    virtual ~ISelectable() = default;
    virtual bool containsPoint(Pt world_pos) const = 0;
};

// ============================================================================
// IDraggable — can be repositioned via drag operations
// ============================================================================

struct IDraggable {
    virtual ~IDraggable() = default;
    virtual Pt getPosition() const = 0;
    virtual void setPosition(Pt pos) = 0;
    virtual Pt getSize() const = 0;
    virtual void setSize(Pt size) = 0;
};

// ============================================================================
// IPersistable — has stable identity for serialization linkage
// ============================================================================

struct IPersistable {
    virtual ~IPersistable() = default;
    virtual const std::string& getId() const = 0;
};
