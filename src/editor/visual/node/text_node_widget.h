#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include <string>
#include <optional>
#include <cstdint>

struct Node;

namespace visual {

/// Text annotation widget (render_hint="text").
/// Resizable, no ports, renders multiline text with border.
class TextNodeWidget : public Widget {
public:
    explicit TextNodeWidget(const ::Node& data);

    std::string_view id() const override { return node_id_; }
    bool isClickable() const override { return true; }
    RenderLayer renderLayer() const override { return RenderLayer::Text; }

    const std::string& nodeId() const { return node_id_; }
    const std::string& name() const { return name_; }
    const std::string& text() const { return text_; }
    float baseFontSize() const { return font_size_base_; }

    bool isResizable() const override { return true; }

    void setText(const std::string& t) { text_ = t; }

    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string node_id_;
    std::string name_;
    std::string text_;
    float font_size_base_;
    std::optional<uint32_t> custom_fill_;
};

} // namespace visual
