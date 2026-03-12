#pragma once

#include "editor/visual/node/node.h"
#include <string>

namespace an24 {

class TextVisualNode : public VisualNode {
public:
    TextVisualNode(const Node& node);

    RenderLayer renderLayer() const override { return RenderLayer::Text; }
    bool isResizable() const override { return true; }

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    std::string text_;
    float font_size_base_;
};

} // namespace an24
