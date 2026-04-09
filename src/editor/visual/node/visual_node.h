#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "visual/container/linear_layout.h"
#include "visual/container/container.h"
#include "visual/container/port_row.h"
#include "visual/widgets/content_widgets.h"
#include "visual/primitives/primitives.h"
#include "visual/node/bounds.h"
#include "visual/node/layout_context.h"
#include "ui/core/interned_id.h"
#include "visual/node/port_layout_resolver.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>

namespace visual {

enum class NodeInteractionType {
    Toggle,
    Slider,
    Knob
};

struct NodeInteractionHit {
    NodeInteractionType type = NodeInteractionType::Toggle;
    float content_local_x = 0.0f;
};

struct NodeVisualState {
    bool selected = false;
    bool has_interactive_content = false;
};

/// Node widget in the new scene graph.
/// Root widget added to Scene. Clickable (tracked in Grid for hit testing).
/// Owns its layout tree: header, port rows, content area, type name footer.
class NodeWidget : public Widget {
public:
    NodeWidget(const bp2::Blueprint::Node& data,
               const bp2::Interface& render_iface,
               const ui::StringInterner& interner);

    std::string_view id() const override { return interner_->resolve(node_iid_); }
    bool isClickable() const override { return true; }
    bool isResizable() const override { return true; }

    std::string_view nodeId() const { return interner_->resolve(node_iid_); }
    const std::string& name() const { return name_; }
    const std::string& typeName() const { return type_name_; }

    /// Update content state (gauge value, switch state, etc.)
    void updateContent(const NodeContent& content);

    /// Access ports by name
    Port* port(std::string_view name) const;
    Port* portByName(std::string_view port_name,
                     std::string_view wire_id = {}) const override;
    const std::vector<Port*>& ports() const { return ports_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

    /// Content area bounds relative to the node origin (for ImGui overlay).
    /// Returns zero-size Bounds if no content widget exists.
    Bounds contentBounds() const;

    /// Content widget (if any). nullptr for nodes without interactive content.
    Widget* contentWidget() const { return content_widget_; }

    /// Query node-local interaction hit in content area.
    /// Returns empty when no interactive content was hit.
    std::optional<NodeInteractionHit> query_interaction(Pt world_pos) const;

    /// Derive per-frame visual state for this node from render context.
    NodeVisualState visual_state(const RenderContext& ctx) const;

    /// Custom fill color (nullopt = use theme default)
    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

private:
    ui::InternedId node_iid_;
    const ui::StringInterner* interner_;
    std::string name_;
    std::string type_name_;

    /// Non-owning pointers to child widgets (owned via widget tree)
    Column* layout_ = nullptr;
    Widget* content_widget_ = nullptr;
    std::vector<Port*> ports_;

    /// Layout context shared with PortRow children for edge-anchoring.
    /// Populated before layout() calls propagate to children.
    LayoutContext layout_ctx_;

    std::optional<uint32_t> custom_fill_;

    void buildLayout(const bp2::Blueprint::Node& data,
                     const bp2::Interface& render_iface,
                     const ui::StringInterner& interner);
    void buildStandardLayout(const bp2::Blueprint::Node& data,
                             const bp2::Interface& render_iface,
                             const ui::StringInterner& interner);
    void buildVerticalToggleLayout(const bp2::Blueprint::Node& data,
                                   const bp2::Interface& render_iface,
                                   const ui::StringInterner& interner);
    void buildPortRow(std::string_view left_name, PortType left_type,
                      std::string_view right_name, PortType right_type);
    void buildPortInColumn(Widget* col, std::string_view name, PortType type, bp2::PortSide logical_side, bp2::PortLayoutSide layout_side);
    void buildFourSidedLayout(const bp2::Blueprint::Node& data,
                              const bp2::Interface& render_iface,
                              const ui::StringInterner& interner);

    void buildHorizontalPortStrip(const std::vector<ResolvedPort>& ports);
};

} // namespace visual
