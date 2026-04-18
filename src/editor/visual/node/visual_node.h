#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "visual/primitives/primitives.h"
#include "visual/node/bounds.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "editor/visual/presentation/node_slot_layout.h"
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

/// Describes one port entry with its associated label widget, resolved layout
/// side, and logical side. Owned as direct children of NodeWidget.
struct PortEntry {
    Port* port = nullptr;
    Label* label = nullptr;
    bp2::PortLayoutSide layout_side = bp2::PortLayoutSide::Left;
    bp2::Direction logical_direction = bp2::Direction::Input;
};

/// Node widget — flat slot-based layout.
///
/// Owns ports, labels, header and footer as direct children.
/// layout() places them into computed slot regions (header, footer,
/// left/right/top/bottom port strips, body/content) without
/// intermediate layout widgets.
class NodeWidget : public Widget {
public:
    NodeWidget(const bp2::Blueprint::Node& data,
               const bp2::Interface& render_iface,
               const ui::StringInterner& interner,
               const NodeContent& content);

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
    const std::vector<Port*>& ports() const { return port_ptrs_; }

    Pt preferredSize(IDrawList* dl) const override;
    Pt minimumNodeSize() const;
    void layout(float w, float h) override;
    void onLocalPosChanged() override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;
    void renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const override;

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

    /// Snapshot of current cached content (for test inspection / round-trip)
    NodeContent currentContent() const {
        NodeContent c;
        c.type = cached_content_type_;
        c.min = cached_content_min_;
        c.max = cached_content_max_;
        c.value = cached_content_value_;
        c.label = cached_content_label_;
        c.state = cached_content_state_;
        c.tripped = cached_content_tripped_;
        c.unit = cached_content_unit_;
        return c;
    }

private:
    ui::InternedId node_iid_;
    const ui::StringInterner* interner_;
    std::string name_;
    std::string type_name_;

    // ---- Flat child widgets (all owned via Widget::children_) ----
    Widget* header_ = nullptr;      ///< HeaderStrip
    Widget* footer_ = nullptr;      ///< FooterTypeLabel

    /// Port entries with associated labels. Port* and Label* are owned as
    /// direct children. The entries are grouped by layout_side for fast
    /// iteration during layout.
    std::vector<PortEntry> port_entries_;

    /// Flat port pointer list for external API (ports()).
    std::vector<Port*> port_ptrs_;

    // ---- Resolved port layout (persisted from construction) ----
    ResolvedLayout resolved_layout_;

    // ---- Content ----
    bp2::NodeContentType cached_content_type_ = bp2::NodeContentType::None;
    float cached_content_max_ = 0.0f;
    float cached_content_min_ = 0.0f;
    float cached_content_value_ = 0.0f;
    std::string cached_content_label_;
    bool cached_content_state_ = false;
    bool cached_content_tripped_ = false;
    std::string cached_content_unit_;

    std::optional<uint32_t> custom_fill_;
    editor::presentation::SemanticSceneSnapshot content_semantic_snapshot_;
    bool render_content_from_semantic_snapshot_ = false;
    Bounds content_bounds_{};

    /// Content alignment within its body cell.
    float content_align_x_ = 0.5f;
    float content_align_y_ = 0.5f;
    bool content_reserve_width_ = true;
    bool content_reserve_height_ = true;
    Pt content_preferred_size_{};

    // ---- Slot-driven shell layout state ----
    editor::presentation::NodeShellLayoutSpec shell_spec_{};
    mutable editor::presentation::NodeShellLayout measured_shell_{};

    void build(const bp2::Blueprint::Node& data,
               const bp2::Interface& render_iface,
               const ui::StringInterner& interner,
               const NodeContent& content);
    void configure_content_geometry(bp2::NodeContentType content_type);
    void refresh_content_semantic_snapshot();

    editor::presentation::NodeShellLayoutSpec build_shell_spec(IDrawList* dl) const;
    void apply_shell_layout(const editor::presentation::NodeShellLayout& shell);
};

} // namespace visual
