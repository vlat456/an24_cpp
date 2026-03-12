#pragma once

#include "visual/node/widget_base.h"
#include "data/port.h"
#include "data/pt.h"
#include <string>
#include <cstdint>

struct IDrawList;

// ============================================================================
// VisualPort — self-drawing port widget
// ============================================================================
// Each port renders itself as a filled circle with type-based color.
// Attached to a VisualNode as a child widget.
//
// Identity: (name, target_port)
//   - For normal ports: name == logical port name, target_port is empty
//   - For Bus aliases:  name == wire ID, target_port == "v"
//
// Position: world coordinates, set by parent node during layout.

class VisualPort : public Widget {
public:
    static constexpr float RADIUS = 6.0f;
    static constexpr float HIT_RADIUS = 10.0f;
    static constexpr float LABEL_FONT_SIZE = 9.0f;  // Font::Small
    static constexpr float LABEL_GAP = 3.0f;

    VisualPort(const std::string& name,
               PortSide side,
               PortType type = PortType::Any,
               const std::string& target_port = "");

    // --- Identity ---
    const std::string& name() const { return name_; }
    const std::string& targetPort() const { return target_port_; }
    /// Logical port name: target_port if set, otherwise name
    const std::string& logicalName() const {
        return target_port_.empty() ? name_ : target_port_;
    }
    bool isAlias() const { return !target_port_.empty(); }

    // --- Type & Side ---
    PortType type() const { return type_; }
    void setType(PortType t) { type_ = t; }
    PortSide side() const { return side_; }

    // --- Color ---
    uint32_t color() const;

    // --- World position ---
    Pt worldPosition() const { return world_pos_; }
    void setWorldPosition(Pt pos) { world_pos_ = pos; }

    // --- Bounds (for HitTester) ---
    Bounds calcBounds() const;

    // --- Compatibility ---
    /// Check if this port can connect to another (type + side rules)
    bool isCompatibleWith(const VisualPort& other) const;

    /// Check if port types are compatible (Any is wildcard)
    static bool areTypesCompatible(PortType a, PortType b);

    /// Check if port sides allow connection
    static bool areSidesCompatible(PortSide a, PortSide b);

    // --- Widget: self-drawing ---
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    // --- Optional label (shown next to circle) ---
    const std::string& label() const { return label_; }
    void setLabel(const std::string& label) { label_ = label; }

    enum class LabelSide { Left, Right, None };
    LabelSide labelSide() const { return label_side_; }
    void setLabelSide(LabelSide ls) { label_side_ = ls; }

private:
    std::string name_;
    std::string target_port_;
    std::string label_;
    PortSide side_;
    PortType type_;
    Pt world_pos_;
    LabelSide label_side_ = LabelSide::None;
};
