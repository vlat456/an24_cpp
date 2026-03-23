#include "visual_node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "editor/layout_constants.h"
#include "visual/node/bounds.h"
#include "visual/snap.h"
#include "data/node.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

NodeWidget::NodeWidget(const ::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
    , type_name_(data.type_name)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);
    buildLayout(data, interner);

    // Auto-size: compute preferred, snap to grid
    Pt preferred = preferredSize(nullptr);

    float w = preferred.x;
    float h = preferred.y;

    if (data.has_explicit_size()) {
        // Trust the user's explicit size — only enforce a hard minimum
        // (PORT_LAYOUT_GRID) to prevent degenerate zero-area nodes.
        // The old code rejected explicit sizes smaller than preferred,
        // which silently reverted content-heavy nodes (AZS, Voltmeter,
        // HoldButton) back to auto-size after every scene rebuild.
        if (data.explicit_size().x >= editor_constants::PORT_LAYOUT_GRID) w = data.explicit_size().x;
        if (data.explicit_size().y >= editor_constants::PORT_LAYOUT_GRID) h = data.explicit_size().y;
    }
    spdlog::info("[DEBUG-WIDGET] NodeWidget: node={} type={} preferred=({},{}) has_explicit_size={} -> final=({},{})",
                 data.name, data.type_name, preferred.x, preferred.y,
                 data.has_explicit_size(), w, h);

    // Snap to layout grid (round up to nearest PORT_LAYOUT_GRID)
    Pt snapped = editor_math::snap_size_to_layout_grid(Pt(w, h));
    w = snapped.x;
    h = snapped.y;

    layout(w, h);
}

NodeWidget::NodeWidget(const bp2::Blueprint::Node& data, const ui::StringInterner& interner)
    : NodeWidget([
        &]() {
            Node node;
            node.id = data.id;
            node.name = data.name;
            node.type_name = std::string(interner.resolve(data.type));
            node.render_hint = data.render_hint;
            node.expandable = data.expandable;
            node.collapsed = data.collapsed;
            node.blueprint_path = data.blueprint_path;
            node.group_id = data.group_id;
            node.pos = ui::Pt(data.x, data.y);

            if (data.width.has_value() && data.height.has_value()) {
                node.set_explicit_size(ui::Pt(*data.width, *data.height));
            }

            node.inputs = data.inputs;
            node.outputs = data.outputs;

            for (const auto& ov : data.layout_overrides) {
                PortLayoutOverride lo;
                lo.port_name = ov.port_name;
                if (ov.side.has_value()) {
                    lo.side = parse_port_layout_side(*ov.side);
                }
                if (ov.position.has_value()) {
                    lo.position = static_cast<uint8_t>(*ov.position);
                }
                node.layout_overrides.push_back(std::move(lo));
            }

            node.node_content.type = static_cast<NodeContentType>(data.content_type);
            node.node_content.label = data.content_label;
            node.node_content.value = data.content_value;
            node.node_content.min = data.content_min;
            node.node_content.max = data.content_max;
            node.node_content.unit = data.content_unit;
            node.node_content.state = data.content_state;
            node.node_content.tripped = data.content_tripped;

            if (data.has_color) {
                NodeColor c;
                c.r = data.color_r;
                c.g = data.color_g;
                c.b = data.color_b;
                c.a = data.color_a;
                node.color = c;
            }

            return node;
        }(),
        interner)
{}

// ============================================================================
// Layout construction
// ============================================================================

void NodeWidget::buildLayout(const ::Node& data, const ui::StringInterner& interner) {
    layout_ = emplaceChild<Column>();

    // -- Header --
    layout_->emplaceChild<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING);

    // -- Port rows / Content --
    // VerticalToggle uses special layout, but falls back to standard when overrides present
    if (data.node_content.type == NodeContentType::VerticalToggle && data.layout_overrides.empty()) {
        buildVerticalToggleLayout(data, interner);
    } else {
        buildStandardLayout(data, interner);
    }

    // -- Flex spacer pushes footer to bottom when node is resized taller.
    //    Only added when no other flex child exists (e.g., pure port-only nodes),
    //    otherwise the content flex child handles the stretching. --
    if (!content_widget_) {
        layout_->emplaceChild<Spacer>();
    }

    // -- Type name footer --
    layout_->emplaceChild<TypeNameWidget>(type_name_);
}

void NodeWidget::buildStandardLayout(const ::Node& data, const ui::StringInterner& interner) {
    // Fast path: no overrides — use existing paired-row layout
    if (data.layout_overrides.empty()) {
        // Port rows: pair inputs and outputs
        size_t max_ports = std::max(data.inputs.size(), data.outputs.size());
        for (size_t i = 0; i < max_ports; i++) {
            std::string_view left_name;
            std::string_view right_name;
            if (i < data.inputs.size()) {
                left_name = interner.resolve(data.inputs[i].name);
            }
            PortType left_type = (i < data.inputs.size()) ? data.inputs[i].type : PortType::Any;
            if (i < data.outputs.size()) {
                right_name = interner.resolve(data.outputs[i].name);
            }
            PortType right_type = (i < data.outputs.size()) ? data.outputs[i].type : PortType::Any;
            buildPortRow(left_name, left_type, right_name, right_type);
        }

        // Content area (appended below port rows in the root Column)
        if (data.node_content.type == NodeContentType::Gauge) {
            content_widget_ = layout_->emplaceChild<VoltmeterWidget>(
                data.node_content.value, data.node_content.min,
                data.node_content.max, data.node_content.unit);
        } else if (data.node_content.type == NodeContentType::Switch) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            float v_pad = 2.0f;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, v_pad, margin, v_pad});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<SwitchWidget>(
                data.node_content.state, data.node_content.tripped);
        } else if (data.node_content.type == NodeContentType::VerticalToggle) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 5.0f, margin, 5.0f});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<VerticalToggleWidget>(
                data.node_content.state, data.node_content.tripped);
        } else if (data.node_content.type == NodeContentType::Slider) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            float v_pad = 2.0f;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, v_pad, margin, v_pad});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<SliderWidget>(
                data.node_content.value, data.node_content.min,
                data.node_content.max);
        } else if (data.node_content.type != NodeContentType::None) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 0, margin, 0});
            container->setFlexGrow(1.0f);
            if (!data.node_content.label.empty()) {
                content_widget_ = container->emplaceChild<Label>(
                    data.node_content.label, 10.0f, (uint32_t)0x00000000);
            } else {
                content_widget_ = container->emplaceChild<Spacer>();
            }
        }
    } else {
        // Slow path: four-sided layout with overrides.
        // Content is placed inside the center column of the body row.
        buildFourSidedLayout(data, interner);
    }
}

void NodeWidget::buildVerticalToggleLayout(const ::Node& data, const ui::StringInterner& interner) {
    auto* main_row = layout_->emplaceChild<Row>();

    // Left column (input ports)
    auto* left_col = main_row->emplaceChild<Column>();
    for (const auto& p : data.inputs) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(left_col, name_sv, p.type, PortSide::Input, PortLayoutSide::Left);
    }

    // Center column (vertical toggle) — flex to push right column to the edge
    auto* center_col = main_row->emplaceChild<Column>();
    center_col->setFlexGrow(1.0f);
    auto* toggle_container = center_col->emplaceChild<Container>(
        Edges{0, 5.0f, 0, 5.0f});
    content_widget_ = toggle_container->emplaceChild<VerticalToggleWidget>(
        data.node_content.state, data.node_content.tripped);

    // Right column (output ports)
    auto* right_col = main_row->emplaceChild<Column>();
    for (const auto& p : data.outputs) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(right_col, name_sv, p.type, PortSide::Output, PortLayoutSide::Right);
    }
}

void NodeWidget::buildPortRow(std::string_view left_name, PortType left_type,
                              std::string_view right_name, PortType right_type) {
    auto* row = layout_->emplaceChild<PairedPortRow>(
        left_name, left_type, right_name, right_type, &layout_ctx_);
    if (row->leftPort())  ports_.push_back(row->leftPort());
    if (row->rightPort()) ports_.push_back(row->rightPort());
}

void NodeWidget::buildPortInColumn(Widget* col, std::string_view name,
                                   PortType type, PortSide logical_side, PortLayoutSide layout_side) {
    auto* row = col->emplaceChild<PortRow>(name, logical_side, type, layout_side, &layout_ctx_);
    if (row->port()) ports_.push_back(row->port());
}

void NodeWidget::buildFourSidedLayout(const ::Node& data, const ui::StringInterner& interner) {
    using namespace editor_constants;
    four_sided_layout_ = true;
    
    ResolvedLayout layout = resolve_port_layout(data.inputs, data.outputs, 
                                                 data.layout_overrides, interner);
    
    // Top port strip
    if (!layout.top.empty()) {
        buildHorizontalPortStrip(layout.top);
    }
    
    // Main body row: [Left ports | Content | Right ports]
    auto* body_row = layout_->emplaceChild<Row>();
    
    // Left column (input ports that stay on left)
    auto* left_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.left) {
        buildPortInColumn(left_col, rp.port_name, rp.type, rp.logical_side, PortLayoutSide::Left);
    }
    
    // Center column: content widget or spacer.
    // Must be flexible so it absorbs remaining width, pushing right_col to
    // the node's right edge (mirroring buildVerticalToggleLayout).
    auto* center = body_row->emplaceChild<Container>(Edges{4, 0, 4, 0});
    center->setFlexGrow(1.0f);

    if (data.node_content.type == NodeContentType::Gauge) {
        content_widget_ = center->emplaceChild<VoltmeterWidget>(
            data.node_content.value, data.node_content.min,
            data.node_content.max, data.node_content.unit);
    } else if (data.node_content.type == NodeContentType::Switch) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        content_widget_ = inner->emplaceChild<SwitchWidget>(
            data.node_content.state, data.node_content.tripped);
    } else if (data.node_content.type == NodeContentType::VerticalToggle) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 5.0f, 0, 5.0f});
        content_widget_ = inner->emplaceChild<VerticalToggleWidget>(
            data.node_content.state, data.node_content.tripped);
    } else if (data.node_content.type == NodeContentType::Slider) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        content_widget_ = inner->emplaceChild<SliderWidget>(
            data.node_content.value, data.node_content.min,
            data.node_content.max);
    } else if (data.node_content.type != NodeContentType::None) {
        if (!data.node_content.label.empty()) {
            content_widget_ = center->emplaceChild<Label>(
                data.node_content.label, 10.0f, (uint32_t)0x00000000);
        } else {
            center->emplaceChild<Spacer>();
        }
    } else {
        center->emplaceChild<Spacer>();
    }
    
    // Right column (output ports that stay on right)
    auto* right_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.right) {
        buildPortInColumn(right_col, rp.port_name, rp.type, rp.logical_side, PortLayoutSide::Right);
    }
    
    // Bottom port strip
    if (!layout.bottom.empty()) {
        buildHorizontalPortStrip(layout.bottom);
    }
}

void NodeWidget::buildHorizontalPortStrip(const std::vector<ResolvedPort>& ports) {
    if (ports.empty()) return;

    PortLayoutSide side = ports[0].layout_side;
    auto* strip = layout_->emplaceChild<HorizontalPortStrip>(side, &layout_ctx_);

    for (const auto& rp : ports) {
        auto* port_w = strip->addPort(rp.port_name, rp.logical_side, rp.type);
        ports_.push_back(port_w);
    }
}

// ============================================================================
// Content updates
// ============================================================================

void NodeWidget::updateContent(const ::NodeContent& content) {
    if (content_widget_) content_widget_->updateFromContent(content);
}

::Bounds NodeWidget::contentBounds() const {
    if (!content_widget_) return {};
    Pt wp = content_widget_->worldPos();
    Pt np = worldPos();
    Pt sz = content_widget_->size();
    return { wp.x - np.x, wp.y - np.y, sz.x, sz.y };
}

Port* NodeWidget::port(std::string_view name) const {
    for (auto* p : ports_) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

Port* NodeWidget::portByName(std::string_view port_name,
                             std::string_view /*wire_id*/) const {
    for (auto* p : ports_) {
        if (p->name() == port_name) return p;
    }
    return nullptr;
}

// ============================================================================
// Layout & sizing
// ============================================================================

Pt NodeWidget::preferredSize(IDrawList* dl) const {
    if (!layout_) return Pt(0, 0);
    Pt ps = layout_->preferredSize(dl);
    
    // In four-sided layout the body is a Row: [left_col | center(flex) | right_col].
    // The center column is flexible, so the Row's preferred width only sums the
    // port columns.  Add the content widget's minimum width so the node is never
    // too narrow.
    if (four_sided_layout_ && content_widget_) {
        Pt cps = content_widget_->preferredSize(dl);
        if (cps.x > 0) {
            constexpr float center_margin = 8.0f; // Edges{4,0,4,0}
            ps.x = std::max(ps.x, ps.x + cps.x + center_margin);
        }
    }
    
    return ps;
}

void NodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));

    // Populate layout context BEFORE child layout so that PairedPortRow/PortRow
    // and HorizontalPortStrip children can position ports at node edges during
    // their own layout() calls.
    layout_ctx_.node_width  = w;
    layout_ctx_.node_height = h;

    if (layout_) {
        layout_->layout(w, h);
    }
}

// ============================================================================
// Rendering
// ============================================================================

void NodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * zoom;

    // Body fill
    uint32_t fill = custom_fill_.value_or(render_theme::COLOR_BODY_FILL);
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Children (header, ports, content, footer) rendered by renderTree()
}

void NodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    handle_renderer::draw_selection_border(*dl, ctx, *this, screen_min, screen_max, rounding);
    handle_renderer::draw_resize_handles(*dl, ctx, *this);
}

} // namespace visual
