#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "visual/container/linear_layout.h"
#include "visual/container/container.h"
#include "visual/container/port_row.h"
#include "visual/primitives/primitives.h"
#include "visual/node/bounds.h"
#include "visual/node/layout_context.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
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

struct NodeVisualState {
    bool selected = false;
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
    void onLocalPosChanged() override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

    /// Content area bounds relative to the node origin (for ImGui overlay).
    /// Returns zero-size Bounds if the node has no semantic content region.
    Bounds contentBounds() const;

     const editor::presentation::SemanticSceneSnapshot& content_semantic_snapshot() const {
         return content_semantic_snapshot_;
      }

     bool renders_content_from_semantic_snapshot() const {
         return render_content_from_semantic_snapshot_;
     }

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
    Widget* content_container_ = nullptr; ///< innermost widget wrapping the content spacer
    std::vector<Port*> ports_;
    
    /// Cached content type for semantic snapshot building
    bp2::NodeContentType cached_content_type_ = bp2::NodeContentType::None;
    float cached_content_max_ = 0.0f;
    float cached_content_min_ = 0.0f;
    float cached_content_value_ = 0.0f;
    std::string cached_content_label_;
    bool cached_content_state_ = false;
    bool cached_content_tripped_ = false;
    std::string cached_content_unit_;

    /// Layout context shared with PortRow children for edge-anchoring.
    /// Populated before layout() calls propagate to children.
    LayoutContext layout_ctx_;

    std::optional<uint32_t> custom_fill_;
    editor::presentation::SemanticSceneSnapshot content_semantic_snapshot_;
    bool render_content_from_semantic_snapshot_ = false;
    Bounds content_bounds_{};

    /// Content alignment within its container cell.
    /// 0.0 = start (left/top), 0.5 = center, 1.0 = end (right/bottom).
    float content_align_x_ = 0.5f;
    float content_align_y_ = 0.5f;
    Pt content_preferred_size_{};

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
    void configure_content_geometry(bp2::NodeContentType content_type);
    Bounds compute_content_bounds_from_layout() const;
    void refresh_content_semantic_snapshot();
};

} // namespace visual
