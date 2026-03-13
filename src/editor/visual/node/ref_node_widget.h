#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include <string>
#include <optional>
#include <cstdint>

struct Node;

namespace visual {

/// Reference node widget (e.g., ground symbol).
/// Single centered port, minimal box + text rendering.
class RefNodeWidget : public Widget {
public:
    explicit RefNodeWidget(const ::Node& data);

    std::string_view id() const override { return node_id_; }
    bool isClickable() const override { return true; }

    const std::string& nodeId() const { return node_id_; }
    const std::string& name() const { return name_; }

    Port* port() const { return port_; }
    Port* port(const std::string& name) const;
    Port* portByName(std::string_view port_name,
                     std::string_view wire_id = {}) const override;

    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string node_id_;
    std::string name_;
    std::string type_name_;

    Port* port_ = nullptr;
    std::optional<uint32_t> custom_fill_;

    void buildLayout(const ::Node& data);
    void positionPort();
};

} // namespace visual
