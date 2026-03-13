#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include <string>
#include <optional>
#include <cstdint>

struct Node;

namespace visual {

/// Group container widget (render_hint="group").
/// Resizable, no ports. Renders behind other nodes (RenderLayer::Group).
/// Custom hit testing: only responds to clicks on the border/title bar,
/// allowing clicks to pass through to nodes inside the group.
class GroupNodeWidget : public Widget {
public:
    explicit GroupNodeWidget(const ::Node& data);

    std::string_view id() const override { return node_id_; }
    bool isClickable() const override { return true; }
    RenderLayer renderLayer() const override { return RenderLayer::Group; }

    const std::string& nodeId() const { return node_id_; }
    const std::string& name() const { return name_; }

    bool isResizable() const override { return true; }

    /// Border-only hit testing: returns true only if the point is on the
    /// title bar or within the border margin. Interior clicks pass through.
    bool containsBorder(Pt world_p) const;

    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string node_id_;
    std::string name_;
    std::optional<uint32_t> custom_fill_;
};

} // namespace visual
