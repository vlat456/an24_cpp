#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "data/port.h"
#include <string_view>
#include <cstdint>

namespace visual {

/// Port widget in the new scene graph.
/// Child of a Node. Clickable (tracked in Grid for hit testing).
/// Renders as a filled circle with type-based color.
class Port : public Widget {
public:
    Port(std::string_view name, PortSide side, PortType type);

    std::string_view id() const override { return name_; }
    bool isClickable() const override { return true; }
    /// Ports are looked up via portByName(), not scene.find().
    /// Returning false prevents alias-port IDs from shadowing wire IDs
    /// in the scene's id_index_.
    bool isIndexable() const override { return false; }

    std::string_view name() const { return name_; }
    PortSide side() const { return side_; }
    PortType type() const { return type_; }

    uint32_t color() const;

    /// Check if port sides allow connection (Input <-> Output, InOut connects to anything)
    static bool areSidesCompatible(PortSide a, PortSide b) {
        if (a == PortSide::InOut || b == PortSide::InOut) return true;
        return a != b;
    }

    /// Check if port types are compatible (Any is wildcard)
    static bool areTypesCompatible(PortType a, PortType b) {
        if (a == PortType::Any || b == PortType::Any) return true;
        return a == b;
    }

    Pt preferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;

    static constexpr float RADIUS = 4.0f;
    static constexpr float LABEL_GAP = 3.0f;
    static constexpr float LABEL_FONT_SIZE = 9.0f;

private:
    std::string_view name_;
    PortSide side_;
    PortType type_;
};

} // namespace visual
