#include "visual_node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "editor/layout_constants.h"
#include "visual/node/bounds.h"
#include "visual/snap.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

NodeWidget::NodeWidget(const bp2::Blueprint::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
    , type_name_(std::string(interner.resolve(data.type)))
{
    if (data.has_color) {
        NodeColor c;
        c.r = data.color_r;
        c.g = data.color_g;
        c.b = data.color_b;
        c.a = data.color_a;
        custom_fill_ = c.to_uint32();
    }

    setLocalPos(Pt(data.x, data.y));
    buildLayout(data, interner);

    // Auto-size: compute preferred, snap to grid
    Pt preferred = preferredSize(nullptr);

    float w = preferred.x;
    float h = preferred.y;

    bool has_explicit = data.width.has_value() && data.height.has_value();
    if (has_explicit) {
        // Trust the user's explicit size — only enforce a hard minimum
        // (PORT_LAYOUT_GRID) to prevent degenerate zero-area nodes.
        if (*data.width >= editor_constants::PORT_LAYOUT_GRID) w = *data.width;
        if (*data.height >= editor_constants::PORT_LAYOUT_GRID) h = *data.height;
    }
    spdlog::debug("[widget] NodeWidget layout: node='{}' type='{}' preferred=({},{}) explicit_size={} final=({},{})",
                  data.name, type_name_, preferred.x, preferred.y,
                  has_explicit, w, h);

    // Snap to layout grid (round up to nearest PORT_LAYOUT_GRID)
    Pt snapped = editor_math::snap_size_to_layout_grid(Pt(w, h));
    w = snapped.x;
    h = snapped.y;

    layout(w, h);
}

// ============================================================================
// Layout construction
// ============================================================================

/// Helper: resolve layout overrides from bp2 format to PortLayoutOverride vector
static std::vector<PortLayoutOverride> resolve_bp2_layout_overrides(
    const std::vector<bp2::Blueprint::Node::PortLayoutOverride>& bp2_overrides) {
    std::vector<PortLayoutOverride> result;
    result.reserve(bp2_overrides.size());
    for (const auto& ov : bp2_overrides) {
        PortLayoutOverride lo;
        lo.port_name = ov.port_name;
        if (ov.side.has_value()) {
            lo.side = parse_port_layout_side(*ov.side);
        }
        if (ov.position.has_value()) {
            lo.position = static_cast<uint8_t>(*ov.position);
        }
        result.push_back(std::move(lo));
    }
    return result;
}

/// Helper: get NodeContentType from bp2 enum
static NodeContentType to_node_content_type(bp2::NodeContentType t) {
    return static_cast<NodeContentType>(t);
}

void NodeWidget::buildLayout(const bp2::Blueprint::Node& data, const ui::StringInterner& interner) {
    layout_ = emplaceChild<Column>();

    // -- Header --
    layout_->emplaceChild<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING);

    NodeContentType content_type = to_node_content_type(data.content_type);

    // -- Port rows / Content --
    // VerticalToggle uses special layout, but falls back to standard when overrides present
    if (content_type == NodeContentType::VerticalToggle && data.layout_overrides.empty()) {
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

void NodeWidget::buildStandardLayout(const bp2::Blueprint::Node& data, const ui::StringInterner& interner) {
    NodeContentType content_type = to_node_content_type(data.content_type);

    // Fast path: no overrides — use existing paired-row layout
    if (data.layout_overrides.empty()) {
        // Port rows: pair inputs and outputs.
        // [BUG-2] InOut ports appear in BOTH inputs and outputs arrays;
        // filter duplicates from outputs so they only render on the left side.
        std::vector<EditorPort> right_ports;
        right_ports.reserve(data.outputs.size());
        for (const auto& p : data.outputs) {
            if (p.side == PortSide::InOut) continue;  // already in inputs
            right_ports.push_back(p);
        }

        size_t max_ports = std::max(data.inputs.size(), right_ports.size());
        for (size_t i = 0; i < max_ports; i++) {
            std::string_view left_name;
            std::string_view right_name;
            if (i < data.inputs.size()) {
                left_name = interner.resolve(data.inputs[i].name);
            }
            PortType left_type = (i < data.inputs.size()) ? data.inputs[i].type : PortType::Any;
            if (i < right_ports.size()) {
                right_name = interner.resolve(right_ports[i].name);
            }
            PortType right_type = (i < right_ports.size()) ? right_ports[i].type : PortType::Any;
            buildPortRow(left_name, left_type, right_name, right_type);
        }

        // Content area (appended below port rows in the root Column)
        if (content_type == NodeContentType::Gauge) {
            content_widget_ = layout_->emplaceChild<VoltmeterWidget>(
                data.content_value, data.content_min,
                data.content_max, data.content_unit);
        } else if (content_type == NodeContentType::Switch) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            float v_pad = 2.0f;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, v_pad, margin, v_pad});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<SwitchWidget>(
                data.content_state, data.content_tripped);
        } else if (content_type == NodeContentType::VerticalToggle) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 5.0f, margin, 5.0f});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<VerticalToggleWidget>(
                data.content_state, data.content_tripped);
        } else if (content_type == NodeContentType::Slider) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            float v_pad = 2.0f;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, v_pad, margin, v_pad});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<SliderWidget>(
                data.content_value, data.content_min,
                data.content_max);
        } else if (content_type == NodeContentType::Indicator) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 2.0f, margin, 2.0f});
            container->setFlexGrow(1.0f);
            content_widget_ = container->emplaceChild<IndicatorWidget>(data.content_value);
        } else if (content_type == NodeContentType::Knob) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 2.0f, margin, 2.0f});
            container->setFlexGrow(1.0f);
            int pos = static_cast<int>(data.content_value);
            int num = static_cast<int>(data.content_max);
            if (num < 2) num = 2;
            content_widget_ = container->emplaceChild<KnobWidget>(pos, num);
        } else if (content_type != NodeContentType::None) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 0, margin, 0});
            container->setFlexGrow(1.0f);
            if (!data.content_label.empty()) {
                content_widget_ = container->emplaceChild<Label>(
                    data.content_label, 10.0f, (uint32_t)0x00000000);
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

void NodeWidget::buildVerticalToggleLayout(const bp2::Blueprint::Node& data, const ui::StringInterner& interner) {
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
        data.content_state, data.content_tripped);

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

void NodeWidget::buildFourSidedLayout(const bp2::Blueprint::Node& data, const ui::StringInterner& interner) {
    using namespace editor_constants;

    auto overrides = resolve_bp2_layout_overrides(data.layout_overrides);
    ResolvedLayout layout = resolve_port_layout(data.inputs, data.outputs,
                                                 overrides, interner);
    
    NodeContentType content_type = to_node_content_type(data.content_type);

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

    if (content_type == NodeContentType::Gauge) {
        content_widget_ = center->emplaceChild<VoltmeterWidget>(
            data.content_value, data.content_min,
            data.content_max, data.content_unit);
    } else if (content_type == NodeContentType::Switch) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        content_widget_ = inner->emplaceChild<SwitchWidget>(
            data.content_state, data.content_tripped);
    } else if (content_type == NodeContentType::VerticalToggle) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 5.0f, 0, 5.0f});
        content_widget_ = inner->emplaceChild<VerticalToggleWidget>(
            data.content_state, data.content_tripped);
    } else if (content_type == NodeContentType::Slider) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        content_widget_ = inner->emplaceChild<SliderWidget>(
            data.content_value, data.content_min,
            data.content_max);
    } else if (content_type == NodeContentType::Indicator) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        content_widget_ = inner->emplaceChild<IndicatorWidget>(data.content_value);
    } else if (content_type == NodeContentType::Knob) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        int pos = static_cast<int>(data.content_value);
        int num = static_cast<int>(data.content_max);
        if (num < 2) num = 2;
        content_widget_ = inner->emplaceChild<KnobWidget>(pos, num);
    } else if (content_type != NodeContentType::None) {
        if (!data.content_label.empty()) {
            content_widget_ = center->emplaceChild<Label>(
                data.content_label, 10.0f, (uint32_t)0x00000000);
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

std::optional<NodeInteractionHit> NodeWidget::query_interaction(Pt world_pos) const {
    if (!content_widget_) {
        return std::nullopt;
    }

    const Bounds cb = contentBounds();
    const Pt pos = worldPos();
    const float lx = world_pos.x - pos.x;
    const float ly = world_pos.y - pos.y;
    if (!cb.contains(lx, ly)) {
        return std::nullopt;
    }

    if (dynamic_cast<const SliderWidget*>(content_widget_) != nullptr) {
        NodeInteractionHit hit;
        hit.type = NodeInteractionType::Slider;
        hit.content_local_x = lx - cb.x;
        return hit;
    }
    if (dynamic_cast<const KnobWidget*>(content_widget_) != nullptr) {
        NodeInteractionHit hit;
        hit.type = NodeInteractionType::Knob;
        return hit;
    }
    if (content_widget_->isToggleable()) {
        NodeInteractionHit hit;
        hit.type = NodeInteractionType::Toggle;
        return hit;
    }

    return std::nullopt;
}

NodeVisualState NodeWidget::visual_state(const RenderContext& ctx) const {
    NodeVisualState state;
    state.selected = ctx.isNodeSelected(this);
    state.has_interactive_content =
        dynamic_cast<const SliderWidget*>(content_widget_) != nullptr ||
        dynamic_cast<const KnobWidget*>(content_widget_) != nullptr ||
        (content_widget_ != nullptr && content_widget_->isToggleable());
    return state;
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
    
    // When content lives inside a flex container within a Row
    // (both VerticalToggle layout and four-sided layout), the flex child
    // contributes 0 to the Row's preferred width, making the node too narrow.
    // Add the content widget's minimum width so the node is never too narrow.
    if (content_widget_ && content_widget_->parent()) {
        // Walk up from content_widget_ to find the nearest flex ancestor.
        // Accumulate horizontal margins along the way.
        float h_margins = 0.0f;
        bool found_flex = false;
        for (auto* w = content_widget_->parent(); w && w != layout_; w = w->parent()) {
            if (w->isFlexible()) {
                found_flex = true;
                break;
            }
        }
        if (found_flex) {
            Pt cps = content_widget_->preferredSize(dl);
            if (cps.x > 0) {
                ps.x += cps.x;
            }
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

    const NodeVisualState state = visual_state(ctx);
    if (!state.selected) {
        return;
    }

    Pt pos = worldPos();
    Pt sz = size();
    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    dl->add_rect_with_rounding_corners(screen_min, screen_max,
        render_theme::COLOR_SELECTED, rounding,
        editor_constants::DRAW_CORNERS_ALL, 2.0f * ctx.zoom);

    Pt mn = worldMin();
    Pt mx = worldMax();
    float r = editor_constants::RESIZE_HANDLE_SIZE * 0.5f * ctx.zoom;
    uint32_t color = render_theme::COLOR_RESIZE_HANDLE;
    Pt corners[] = {
        ctx.world_to_screen(mn),
        ctx.world_to_screen(Pt(mx.x, mn.y)),
        ctx.world_to_screen(Pt(mn.x, mx.y)),
        ctx.world_to_screen(mx),
    };
    for (const auto& c : corners) {
        handle_renderer::draw_handle(*dl, c, r, color);
    }
}

} // namespace visual
