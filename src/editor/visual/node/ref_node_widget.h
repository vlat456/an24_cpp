#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "editor/data/node_state.h"
#include "core/strings/interned_id.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>

namespace visual {

/// Reference node widget (e.g., ground symbol).
/// Single centered port, minimal box + text rendering.
class RefNodeWidget : public Widget {
public:
    RefNodeWidget(const bp2::Blueprint::Node& data,
                  const bp2::Interface& render_iface,
                  const core::StringInterner& interner,
                  std::optional<editor::NodeColor> color = std::nullopt);

    std::string_view id() const override { return interner_->resolve(node_iid_); }
    bool isClickable() const override { return true; }

    std::string_view nodeId() const { return interner_->resolve(node_iid_); }
    const std::string& name() const { return name_; }

    Port* port() const { return port_; }
    Port* port(std::string_view name) const;
    Port* portByName(std::string_view port_name,
                     std::string_view wire_id = {}) const override;

    /// Set which edge the single port is anchored to.
    void setPortLayoutSide(bp2::PortLayoutSide side);

    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

private:
    core::InternedId node_iid_;
    const core::StringInterner* interner_;
    std::string name_;
    core::InternedId type_iid_;

    Port* port_ = nullptr;
    std::optional<uint32_t> custom_fill_;

    void buildLayout(const bp2::Blueprint::Node& data,
                     const bp2::Interface& render_iface,
                     const core::StringInterner& interner);
    void positionPort();

    bp2::PortLayoutSide port_layout_side_ = bp2::PortLayoutSide::Top;
};

} // namespace visual
