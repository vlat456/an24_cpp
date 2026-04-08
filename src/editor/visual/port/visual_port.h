#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "blueprint_v2/blueprint/node_port.h"
#include "json_parser/json_parser.h"
#include <string_view>
#include <cstdint>

namespace visual {

struct PortConstants {
    // Rendering
    static constexpr float RADIUS = 3.0f;
    static constexpr float HIT_RADIUS = 10.0f;
    
    // Labels
    static constexpr float LABEL_FONT_SIZE = 9.0f;
    static constexpr uint32_t LABEL_COLOR = 0xFFAAAAAA;
    static constexpr float LEFT_LABEL_OFFSET = 4.0f;
    static constexpr float RIGHT_LABEL_OFFSET = 5.0f;
    static constexpr float TOP_LABEL_OFFSET = 9.0f;
    static constexpr float BOTTOM_LABEL_OFFSET = 2.5f;
    
    // Arrowheads
    static constexpr float LEFT_ARROW_OFFSET = 2.5f;
    static constexpr float RIGHT_ARROW_OFFSET = 2.0f;
    static constexpr float TOP_ARROW_OFFSET = 2.5f;
    static constexpr float BOTTOM_ARROW_OFFSET = 1.5f;
    static constexpr float ARROW_SIZE = 3.0f;
    static constexpr float ARROW_THICKNESS = 1.5f;
    
    // Layout
    static constexpr float ROW_HEIGHT = 16.0f;
    static constexpr float MIN_GAP = 20.0f;
    static constexpr float LAYOUT_GRID = 16.0f;
};

/// Port widget in the new scene graph.
/// Child of a Node. Clickable (tracked in Grid for hit testing).
/// Renders as a filled circle with type-based color.
class Port : public Widget {
public:
    Port(std::string_view name, bp2::PortSide side, PortType type, bp2::PortLayoutSide layout_side = bp2::PortLayoutSide::Left);

    std::string_view id() const override { return name_; }
    bool isClickable() const override { return true; }
    /// Ports are looked up via portByName(), not scene.find().
    /// Returning false prevents alias-port IDs from shadowing wire IDs
    /// in the scene's id_index_.
    bool isIndexable() const override { return false; }

    std::string_view name() const { return name_; }
    bp2::PortSide side() const { return side_; }
    PortType type() const { return type_; }
    bp2::PortLayoutSide layoutSide() const { return layout_side_; }
    
    void setLayoutSide(bp2::PortLayoutSide side) { layout_side_ = side; }

    uint32_t color() const;

    /// Check if port sides allow connection (Input <-> Output, InOut connects to anything)
    static bool areSidesCompatible(bp2::PortSide a, bp2::PortSide b) {
        if (a == bp2::PortSide::InOut || b == bp2::PortSide::InOut) return true;
        return a != b;
    }

    /// Check if port types are compatible (Any is wildcard)
    static bool areTypesCompatible(PortType a, PortType b) {
        if (a == PortType::Any || b == PortType::Any) return true;
        return a == b;
    }

    Pt preferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string_view name_;
    bp2::PortSide side_;
    PortType type_;
    bp2::PortLayoutSide layout_side_;
};

} // namespace visual
