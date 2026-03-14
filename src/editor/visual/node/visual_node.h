#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "visual/container/linear_layout.h"
#include "visual/container/container.h"
#include "visual/widgets/content_widgets.h"
#include "visual/primitives/primitives.h"
#include "visual/node/bounds.h"
#include "ui/core/interned_id.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>

struct Node;
struct NodeContent;

namespace visual {

/// Node widget in the new scene graph.
/// Root widget added to Scene. Clickable (tracked in Grid for hit testing).
/// Owns its layout tree: header, port rows, content area, type name footer.
class NodeWidget : public Widget {
public:
    explicit NodeWidget(const ::Node& data, const ui::StringInterner& interner);

    std::string_view id() const override { return node_id_; }
    bool isClickable() const override { return true; }

    std::string_view nodeId() const { return node_id_; }
    const std::string& name() const { return name_; }
    const std::string& typeName() const { return type_name_; }

    /// Update content state (gauge value, switch state, etc.)
    void updateContent(const ::NodeContent& content);

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

    /// Custom fill color (nullopt = use theme default)
    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

private:
    std::string_view node_id_;
    std::string name_;
    std::string type_name_;

    /// Non-owning pointers to child widgets (owned via widget tree)
    Column* layout_ = nullptr;
    Widget* content_widget_ = nullptr;
    std::vector<Port*> ports_;

    std::optional<uint32_t> custom_fill_;

    void buildLayout(const ::Node& data, const ui::StringInterner& interner);
    void buildStandardLayout(const ::Node& data, const ui::StringInterner& interner);
    void buildVerticalToggleLayout(const ::Node& data, const ui::StringInterner& interner);
    void buildPortRow(std::string_view left_name, PortType left_type,
                      std::string_view right_name, PortType right_type);
    void buildPortInColumn(Widget* col, std::string_view name, PortType type, bool is_left);
};

} // namespace visual
