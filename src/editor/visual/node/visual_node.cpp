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
#include "blueprint_v2/interface/node_port_projection.h"
#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <variant>

namespace visual {

// ============================================================================
// Header / Footer primitives (private to this TU)
// ============================================================================

namespace {

class HeaderStrip : public Widget {
public:
    explicit HeaderStrip(std::string text) : text_(std::move(text)) {
        setFlexible(false);
        setSize(Pt(0.0f, kHeight));
    }

    Pt preferredSize(IDrawList* dl) const override {
        float text_w = 0.0f;
        if (!text_.empty()) {
            text_w = dl ? dl->calc_text_size(text_.c_str(), kFontSize).x
                        : fallback_text_width(text_, kFontSize);
        }
        return Pt(kPadding * 2.0f + text_w, kHeight);
    }

    Pt minimumSize(IDrawList* /*dl*/) const override {
        return Pt(0.0f, kHeight);
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl) return;

        Pt origin = ctx.world_to_screen(worldPos());
        float zoom = ctx.zoom;
        float visual_h = kVisualHeight * zoom;
        float rounding = editor_constants::NODE_ROUNDING * zoom;
        Pt max(origin.x + size().x * zoom, origin.y + visual_h);
        dl->add_rect_filled_with_rounding_corners(
            origin, max, render_theme::COLOR_HEADER_FILL, rounding, 0x30);

        if (text_.empty()) return;

        float font = kFontSize * zoom;
        Pt text_pos(origin.x + kPadding * zoom,
                    origin.y + (kVisualHeight - kFontSize) * zoom * 0.5f);
        dl->set_clip_rect(origin, max);
        dl->add_text(text_pos, text_.c_str(), render_theme::COLOR_TEXT, font);
        dl->clear_clip();
    }

    void renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl || text_.empty()) return;
        const float font = kFontSize * ctx.zoom;
        Pt origin = ctx.world_to_screen(worldPos());
        Pt text_size = dl->calc_text_size(text_.c_str(), font);
        Pt text_pos(origin.x + kPadding * ctx.zoom,
                    origin.y + (kVisualHeight - kFontSize) * ctx.zoom * 0.5f);
        dl->add_rect(text_pos,
                     Pt(text_pos.x + text_size.x, text_pos.y + text_size.y),
                     0xFF00FFFF,
                     1.0f);
    }

private:
    std::string text_;

    static constexpr float kHeight = 24.0f;
    static constexpr float kVisualHeight = 20.0f;
    static constexpr float kFontSize = 12.0f;
    static constexpr float kPadding = 5.0f;
};

class FooterTypeLabel : public Widget {
public:
    explicit FooterTypeLabel(std::string text) : text_(std::move(text)) {
        setFlexible(false);
        setSize(Pt(0.0f, kHeight));
    }

    Pt preferredSize(IDrawList* dl) const override {
        float text_w = 0.0f;
        if (!text_.empty()) {
            text_w = dl ? dl->calc_text_size(text_.c_str(), kFontSize).x
                        : fallback_text_width(text_, kFontSize);
        }
        return Pt(text_w + kRightPadding, kHeight);
    }

    Pt minimumSize(IDrawList* /*dl*/) const override {
        return Pt(0.0f, kHeight);
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl || text_.empty()) return;

        Pt origin = ctx.world_to_screen(worldPos());
        float zoom = ctx.zoom;
        float font = kFontSize * zoom;
        Pt text_size = dl->calc_text_size(text_.c_str(), font);
        float tx = std::max(origin.x,
                            origin.x + size().x * zoom - text_size.x - kRightPadding * zoom);
        float ty = origin.y + (kHeight * zoom - font) * 0.5f;
        Pt clip_max(origin.x + size().x * zoom, origin.y + kHeight * zoom);
        dl->set_clip_rect(origin, clip_max);
        dl->add_text(Pt(tx, ty), text_.c_str(), render_theme::COLOR_TEXT_DIM, font);
        dl->clear_clip();
    }

    void renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl || text_.empty()) return;
        const float font = kFontSize * ctx.zoom;
        Pt origin = ctx.world_to_screen(worldPos());
        Pt text_size = dl->calc_text_size(text_.c_str(), font);
        float tx = std::max(origin.x,
                            origin.x + size().x * ctx.zoom - text_size.x - kRightPadding * ctx.zoom);
        float ty = origin.y + (kHeight * ctx.zoom - font) * 0.5f;
        dl->add_rect(Pt(tx, ty),
                     Pt(tx + text_size.x, ty + text_size.y),
                     0xFF00FFFF,
                     1.0f);
    }

private:
    std::string text_;

    static constexpr float kHeight = 16.0f;
    static constexpr float kFontSize = 9.0f;
    static constexpr float kRightPadding = 5.0f;
};

// ============================================================================
// Content constants and helpers
// ============================================================================

constexpr float CONTENT_MARGIN_X = 5.0f;
constexpr float CONTENT_MARGIN_Y = 5.0f;

struct TextPaintGeometry {
    Pt pos;
    Pt size;
    float font = 0.0f;
};

struct LinePaintGeometry {
    Pt a;
    Pt b;
};

struct RectPaintGeometry {
    Pt min;
    Pt max;
};

constexpr float DEG2RAD = 3.14159265f / 180.0f;

TextPaintGeometry resolve_text_paint_geometry(const editor::presentation::SceneRenderObject& object,
                                              const Pt& node_pos,
                                              IDrawList& dl,
                                              const RenderContext& ctx) {
    TextPaintGeometry geometry;
    const auto* text_geo = std::get_if<editor::presentation::TextGeometry>(&object.geometry);
    float font_size = (text_geo && text_geo->font_size > 0.0f) ? text_geo->font_size : 10.0f;
    bool center_aligned = text_geo ? text_geo->center_aligned : false;
    float offset_x = text_geo ? text_geo->x : 0.0f;
    float offset_y = text_geo ? text_geo->y : 0.0f;
    geometry.font = font_size * ctx.zoom;
    geometry.pos = ctx.world_to_screen(Pt(node_pos.x + object.bounds.x + offset_x,
                                          node_pos.y + object.bounds.y + offset_y));
    geometry.size = dl.calc_text_size(object.text.c_str(), geometry.font);
    if (center_aligned) {
        // Center text within the element bounds width, offset_x is additional shift from center
        float bounds_center_x = ctx.world_to_screen(Pt(node_pos.x + object.bounds.x + object.bounds.w * 0.5f + offset_x, 0.0f)).x;
        geometry.pos.x = bounds_center_x - geometry.size.x * 0.5f;
    }
    return geometry;
}

LinePaintGeometry resolve_line_paint_geometry(const editor::presentation::SceneRenderObject& object,
                                              const Pt& node_pos,
                                              const RenderContext& ctx) {
    const auto* line_geo = std::get_if<editor::presentation::LineGeometry>(&object.geometry);
    // Geometry origin (0,0) maps to center of element bounds
    float base_x = object.bounds.x + object.bounds.w * 0.5f + (line_geo ? line_geo->cx : 0.0f);
    float base_y = object.bounds.y + object.bounds.h * 0.5f + (line_geo ? line_geo->cy : 0.0f);
    Pt center = ctx.world_to_screen(Pt(node_pos.x + base_x, node_pos.y + base_y));
    float angle = (line_geo ? line_geo->angle_deg : 0.0f) * DEG2RAD;
    float radius_a = (line_geo ? line_geo->inner_radius : 0.0f) * ctx.zoom;
    float radius_b = (line_geo ? line_geo->outer_radius : 0.0f) * ctx.zoom;
    return {
        Pt(center.x + std::cos(angle) * radius_a, center.y - std::sin(angle) * radius_a),
        Pt(center.x + std::cos(angle) * radius_b, center.y - std::sin(angle) * radius_b),
    };
}

RectPaintGeometry resolve_rect_paint_geometry(const editor::presentation::SceneRenderObject& object,
                                              const Pt& node_pos,
                                              const RenderContext& ctx) {
    const auto* rect_geo = std::get_if<editor::presentation::RectGeometry>(&object.geometry);
    float x = object.bounds.x + (rect_geo ? rect_geo->x : 0.0f);
    float y = object.bounds.y + (rect_geo ? rect_geo->y : 0.0f);
    float w = rect_geo ? rect_geo->w : object.bounds.w;
    float h = rect_geo ? rect_geo->h : object.bounds.h;
    return {
        ctx.world_to_screen(Pt(node_pos.x + x, node_pos.y + y)),
        ctx.world_to_screen(Pt(node_pos.x + x + w, node_pos.y + y + h)),
    };
}

/// Helper: resolve layout overrides from bp2 format to PortLayoutOverride vector
std::vector<PortLayoutOverride> resolve_bp2_layout_overrides(
    const std::vector<bp2::Blueprint::Node::PortLayoutOverride>& bp2_overrides) {
    std::vector<PortLayoutOverride> result;
    result.reserve(bp2_overrides.size());
    for (const auto& ov : bp2_overrides) {
        PortLayoutOverride lo;
        lo.port_name = ov.port_name;
        if (ov.side.has_value()) {
            lo.side = bp2::parse_port_layout_side(*ov.side);
        }
        if (ov.position.has_value()) {
            lo.position = static_cast<uint8_t>(*ov.position);
        }
        result.push_back(std::move(lo));
    }
    return result;
}

/// Collect entries from port_entries_ matching a given layout side.
std::vector<PortEntry*> collect_entries_for_side(
    std::vector<PortEntry>& entries, bp2::PortLayoutSide side) {
    std::vector<PortEntry*> result;
    for (auto& e : entries) {
        if (e.layout_side == side) result.push_back(&e);
    }
    return result;
}

editor::presentation::RailEntryMetrics measure_rail_label(std::string_view text, void* user_data) {
    auto* dl = static_cast<IDrawList*>(user_data);
    editor::presentation::RailEntryMetrics metrics;
    metrics.label_text = text;
    metrics.label_width = dl ? dl->calc_text_size(std::string(text).c_str(), PortConstants::LABEL_FONT_SIZE).x
                             : 0.0f;
    metrics.label_height = PortConstants::LABEL_FONT_SIZE;
    return metrics;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

NodeWidget::NodeWidget(const bp2::Blueprint::Node& data,
                       const bp2::Interface& render_iface,
                       const ui::StringInterner& interner,
                       const NodeContent& content,
                       std::optional<editor::NodeColor> color)
    : node_iid_(data.semantic.id)
    , interner_(&interner)
    , name_(data.view.name)
    , type_name_(std::string(interner.resolve(data.semantic.type)))
{
    if (color.has_value()) {
        custom_fill_ = color->to_uint32();
    }

    setLocalPos(Pt(data.layout.x, data.layout.y));
    build(data, render_iface, interner, content);

    Pt min_sz = minimumNodeSize();
    Pt pref_sz = preferredSize(nullptr);

    float w, h;
    bool has_explicit = data.layout.width.has_value() && data.layout.height.has_value();
    if (has_explicit) {
        w = std::max(*data.layout.width, min_sz.x);
        h = std::max(*data.layout.height, min_sz.y);
    } else {
        w = std::max(pref_sz.x, min_sz.x);
        h = std::max(pref_sz.y, min_sz.y);
    }
    spdlog::debug("[widget] NodeWidget layout: node='{}' type='{}' min=({},{}) pref=({},{}) explicit_size={} final=({},{})",
                  data.view.name, type_name_, min_sz.x, min_sz.y,
                  pref_sz.x, pref_sz.y, has_explicit, w, h);

    Pt snapped = editor_math::snap_size_to_layout_grid(Pt(w, h));
    w = snapped.x;
    h = snapped.y;

    layout(w, h);
}

// ============================================================================
// Build — flat construction of children
// ============================================================================

void NodeWidget::build(const bp2::Blueprint::Node& data,
                       const bp2::Interface& render_iface,
                       const ui::StringInterner& interner,
                       const NodeContent& content) {
    // Header
    header_ = emplaceChild<HeaderStrip>(name_);

    // Footer
    footer_ = emplaceChild<FooterTypeLabel>(type_name_);

    // Content geometry — from resolved NodeContent (source of truth: ComponentSpec + params)
    cached_content_type_ = content.type;
    cached_content_min_ = content.min;
    cached_content_max_ = content.max;
    cached_content_value_ = content.value;
    cached_content_label_ = content.label;
    cached_content_state_ = content.state;
    cached_content_unit_ = content.unit;

    if (cached_content_type_ != bp2::NodeContentType::None) {
        configure_content_geometry(cached_content_type_);
    }

    // Resolve port layout — always use the universal resolver.
    const std::vector<bp2::PortDescriptor> input_ports = bp2::derive_input_ports(render_iface);
    const std::vector<bp2::PortDescriptor> output_ports = bp2::derive_output_ports(render_iface);
    auto overrides = resolve_bp2_layout_overrides(data.layout.layout_overrides);
    resolved_layout_ = resolve_port_layout(input_ports, output_ports, overrides, interner);

    // Create port + label children for each side.
    auto create_entries = [&](const std::vector<ResolvedPort>& resolved, bp2::PortLayoutSide side) {
        for (const auto& rp : resolved) {
            PortEntry entry;
            entry.layout_side = side;
            entry.logical_direction = rp.logical_direction;

            // Create port widget
            entry.port = emplaceChild<Port>(rp.port_name, rp.logical_direction, rp.type, side);
            port_ptrs_.push_back(entry.port);

            // Create label widget
            TextAlign align = (side == bp2::PortLayoutSide::Right) ? TextAlign::Right : TextAlign::Left;
            entry.label = emplaceChild<Label>(rp.port_name, PortConstants::LABEL_FONT_SIZE,
                                               PortConstants::LABEL_COLOR, align);

            port_entries_.push_back(entry);
        }
    };

    create_entries(resolved_layout_.left, bp2::PortLayoutSide::Left);
    create_entries(resolved_layout_.right, bp2::PortLayoutSide::Right);
    create_entries(resolved_layout_.top, bp2::PortLayoutSide::Top);
    create_entries(resolved_layout_.bottom, bp2::PortLayoutSide::Bottom);
}

void NodeWidget::configure_content_geometry(bp2::NodeContentType content_type) {
    auto policy = editor::presentation::compile_node_shell_content_policy(
        content_type,
        CONTENT_MARGIN_X,
        CONTENT_MARGIN_Y);
    content_preferred_size_ = policy.preferred_size;
    content_align_x_ = policy.align_x;
    content_align_y_ = policy.align_y;
    content_reserve_width_ = policy.reserve_width;
    content_reserve_height_ = policy.reserve_height;
}

// ============================================================================
// Content updates
// ============================================================================

void NodeWidget::updateContent(const ::NodeContent& content) {
    cached_content_min_ = content.min;
    cached_content_max_ = content.max;
    cached_content_value_ = content.value;
    cached_content_label_ = content.label;
    cached_content_state_ = content.state;
    cached_content_unit_ = content.unit;
    refresh_content_semantic_snapshot();
}

::Bounds NodeWidget::contentBounds() const {
    return content_bounds_;
}

void NodeWidget::refresh_content_semantic_snapshot() {
    content_semantic_snapshot_ = {};
    render_content_from_semantic_snapshot_ = false;
    if (content_preferred_size_.x <= 0.0f && content_preferred_size_.y <= 0.0f) {
        return;
    }

    const Bounds cb = content_bounds_;
    if (cb.w <= 0.0f || cb.h <= 0.0f) {
        return;
    }

    // Build a PresentationSpec directly from cached content state — no fake node
    editor::presentation::PresentationSpec spec;
    spec.node_id = node_iid_;
    spec.frame_kind = editor::presentation::NodeFrameKind::Standard;
    spec.title = name_;
    spec.content_type = cached_content_type_;
    spec.content_min = cached_content_min_;
    spec.content_max = cached_content_max_;
    spec.content_value = cached_content_value_;
    spec.content_label = cached_content_label_;
    spec.content_state = cached_content_state_;
    spec.content_unit = cached_content_unit_;

    // Compile content through the single-authority compiler path
    editor::presentation::NodePresentation presentation =
        editor::presentation::compile_node_presentation(spec);

    // Check if the compiler produced any renderable content
    bool has_content = !presentation.content.children.empty();
    if (!has_content) {
        return;
    }

    // Layout content tree directly within content bounds (no shell)
    const ui::Rect content_rect{cb.x, cb.y, cb.w, cb.h};
    auto placements = editor::presentation::layout_content_tree(
        presentation.content, content_rect);

    content_semantic_snapshot_ = editor::presentation::build_content_semantic_scene_snapshot(
        presentation, placements);
    render_content_from_semantic_snapshot_ = true;
}

NodeVisualState NodeWidget::visual_state(const RenderContext& ctx) const {
    NodeVisualState state;
    state.selected = ctx.isNodeSelected(id());
    return state;
}

Port* NodeWidget::port(std::string_view name) const {
    for (auto* p : port_ptrs_) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

Port* NodeWidget::portByName(std::string_view port_name,
                             std::string_view /*wire_id*/) const {
    for (auto* p : port_ptrs_) {
        if (p->name() == port_name) return p;
    }
    return nullptr;
}

// ============================================================================
// Layout & sizing
// ============================================================================

editor::presentation::NodeShellLayoutSpec NodeWidget::build_shell_spec(IDrawList* dl) const {
    editor::presentation::NodeShellContentPolicy content_policy;
    content_policy.preferred_size = content_preferred_size_;
    content_policy.align_x = content_align_x_;
    content_policy.align_y = content_align_y_;
    content_policy.reserve_width = content_reserve_width_;
    content_policy.reserve_height = content_reserve_height_;
    content_policy.margin_x = CONTENT_MARGIN_X;
    content_policy.margin_y = CONTENT_MARGIN_Y;

    return editor::presentation::compile_node_shell_layout_spec(
        (dl && header_) ? header_->preferredSize(dl).x : 0.0f,
        (dl && footer_) ? footer_->preferredSize(dl).x : 0.0f,
        PortConstants::RADIUS * 2.0f,
        PortConstants::LEFT_LABEL_OFFSET,
        PortConstants::RIGHT_LABEL_OFFSET,
        PortConstants::TOP_LABEL_OFFSET,
        PortConstants::BOTTOM_LABEL_OFFSET,
        PortConstants::MIN_GAP,
        PortConstants::LAYOUT_GRID,
        PortConstants::ROW_HEIGHT,
        resolved_layout_,
        &measure_rail_label,
        dl,
        content_policy,
        24.0f,
        16.0f);
}

Pt NodeWidget::preferredSize(IDrawList* dl) const {
    measured_shell_ = editor::presentation::measure_node_shell(build_shell_spec(dl));
    return measured_shell_.preferred_size;
}

Pt NodeWidget::minimumNodeSize() const {
    measured_shell_ = editor::presentation::measure_node_shell(build_shell_spec(nullptr));
    return editor_math::snap_size_to_layout_grid(measured_shell_.minimum_size);
}

void NodeWidget::apply_shell_layout(const editor::presentation::NodeShellLayout& shell) {
    if (header_) {
        header_->setLocalPos(Pt(shell.header.x, shell.header.y));
        header_->setSize(Pt(shell.header.w, shell.header.h));
    }
    if (footer_) {
        footer_->setLocalPos(Pt(shell.footer.x, shell.footer.y));
        footer_->setSize(Pt(shell.footer.w, shell.footer.h));
    }

    auto apply_rail = [&](const std::vector<PortEntry*>& entries,
                          const std::vector<editor::presentation::RailPlacement>& rail) {
        const size_t count = std::min(entries.size(), rail.size());
        for (size_t i = 0; i < count; ++i) {
            entries[i]->port->setLocalPos(Pt(rail[i].port_bounds.x, rail[i].port_bounds.y));
            entries[i]->port->setSize(Pt(rail[i].port_bounds.w, rail[i].port_bounds.h));
            if (entries[i]->label) {
                entries[i]->label->setLocalPos(Pt(rail[i].label_bounds.x, rail[i].label_bounds.y));
                entries[i]->label->setSize(Pt(rail[i].label_bounds.w, rail[i].label_bounds.h));
            }
        }
    };

    apply_rail(collect_entries_for_side(port_entries_, bp2::PortLayoutSide::Left), shell.left_rail);
    apply_rail(collect_entries_for_side(port_entries_, bp2::PortLayoutSide::Right), shell.right_rail);
    apply_rail(collect_entries_for_side(port_entries_, bp2::PortLayoutSide::Top), shell.top_rail);
    apply_rail(collect_entries_for_side(port_entries_, bp2::PortLayoutSide::Bottom), shell.bottom_rail);

    content_bounds_ = Bounds{shell.content_bounds.x, shell.content_bounds.y,
                             shell.content_bounds.w, shell.content_bounds.h};
}

void NodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    const auto shell = editor::presentation::arrange_node_shell(build_shell_spec(nullptr), Pt(w, h));
    apply_shell_layout(shell);
    refresh_content_semantic_snapshot();
}

void NodeWidget::onLocalPosChanged() {
    Widget::onLocalPosChanged();
    refresh_content_semantic_snapshot();
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

    if (render_content_from_semantic_snapshot_) {
        for (const auto& object : content_semantic_snapshot_.render_objects) {
            if (object.kind != editor::presentation::SceneRenderObjectKind::ContentPaint) {
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Text) {
                const TextPaintGeometry text = resolve_text_paint_geometry(object, pos, *dl, ctx);
                dl->add_text(text.pos,
                             object.text.c_str(),
                             object.fill_color != 0 ? object.fill_color : render_theme::COLOR_TEXT_DIM,
                             text.font);
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Rectangle) {
                const RectPaintGeometry rect = resolve_rect_paint_geometry(object, pos, ctx);
                dl->add_rect_filled(rect.min, rect.max, object.fill_color);
                if (object.stroke_width > 0.0f) {
                    dl->add_rect(rect.min, rect.max, object.stroke_color, object.stroke_width * ctx.zoom);
                }
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Circle) {
                const auto* circle_geo = std::get_if<editor::presentation::CircleGeometry>(&object.geometry);
                // Geometry origin (0,0) maps to center of element bounds
                float cx = object.bounds.x + object.bounds.w * 0.5f + (circle_geo ? circle_geo->cx : 0.0f);
                float cy = object.bounds.y + object.bounds.h * 0.5f + (circle_geo ? circle_geo->cy : 0.0f);
                float radius = circle_geo ? circle_geo->radius : 0.0f;
                Pt center = ctx.world_to_screen(Pt(pos.x + cx, pos.y + cy));
                float r = radius * ctx.zoom;
                dl->add_circle_filled(center, r, object.fill_color, 24);
                if (object.stroke_width > 0.0f) {
                    dl->add_circle(center, r, object.stroke_color, 24);
                }
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Line) {
                const LinePaintGeometry line = resolve_line_paint_geometry(object, pos, ctx);
                dl->add_line(line.a, line.b, object.fill_color, object.stroke_width * ctx.zoom);
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Arc) {
                const auto* arc_geo = std::get_if<editor::presentation::ArcGeometry>(&object.geometry);
                // Geometry origin (0,0) maps to center of element bounds
                float cx = object.bounds.x + object.bounds.w * 0.5f + (arc_geo ? arc_geo->cx : 0.0f);
                float cy = object.bounds.y + object.bounds.h * 0.5f + (arc_geo ? arc_geo->cy : 0.0f);
                float radius = (arc_geo ? arc_geo->radius : 0.0f) * ctx.zoom;
                float start_angle = (arc_geo ? arc_geo->start_angle_deg : 0.0f) * DEG2RAD;
                float sweep_angle = (arc_geo ? arc_geo->sweep_angle_deg : 0.0f) * DEG2RAD;
                Pt center = ctx.world_to_screen(Pt(pos.x + cx, pos.y + cy));
                constexpr int segments = 32;
                Pt points[segments + 1];
                for (int i = 0; i <= segments; ++i) {
                    float t = static_cast<float>(i) / segments;
                    float angle = start_angle + t * sweep_angle;
                    points[i] = Pt(center.x + std::cos(angle) * radius,
                                   center.y - std::sin(angle) * radius);
                }
                dl->add_polyline(points, segments + 1, object.fill_color, object.stroke_width * ctx.zoom);
            }
        }
    }

    // Children (header, footer, ports, labels) rendered by renderTree()
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

void NodeWidget::renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl || !render_content_from_semantic_snapshot_) {
        return;
    }

    Pt pos = worldPos();
    for (const auto& object : content_semantic_snapshot_.render_objects) {
        if (object.kind != editor::presentation::SceneRenderObjectKind::ContentPaint) {
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Text) {
            const TextPaintGeometry text = resolve_text_paint_geometry(object, pos, *dl, ctx);
            dl->add_rect(text.pos,
                         Pt(text.pos.x + text.size.x, text.pos.y + text.size.y),
                         DEBUG_PAINT_BOUNDS_COLOR,
                         1.0f);
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Rectangle) {
            const RectPaintGeometry rect = resolve_rect_paint_geometry(object, pos, ctx);
            dl->add_rect(rect.min, rect.max, DEBUG_PAINT_BOUNDS_COLOR, 1.0f);
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Circle) {
            const auto* circle_geo = std::get_if<editor::presentation::CircleGeometry>(&object.geometry);
            float cx = object.bounds.x + object.bounds.w * 0.5f + (circle_geo ? circle_geo->cx : 0.0f);
            float cy = object.bounds.y + object.bounds.h * 0.5f + (circle_geo ? circle_geo->cy : 0.0f);
            float radius = (circle_geo ? circle_geo->radius : 0.0f) * ctx.zoom;
            Pt center = ctx.world_to_screen(Pt(pos.x + cx, pos.y + cy));
            dl->add_rect(Pt(center.x - radius, center.y - radius),
                         Pt(center.x + radius, center.y + radius),
                         DEBUG_PAINT_BOUNDS_COLOR,
                         1.0f);
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Line) {
            const LinePaintGeometry line = resolve_line_paint_geometry(object, pos, ctx);
            dl->add_rect(Pt(std::min(line.a.x, line.b.x), std::min(line.a.y, line.b.y)),
                         Pt(std::max(line.a.x, line.b.x), std::max(line.a.y, line.b.y)),
                         DEBUG_PAINT_BOUNDS_COLOR,
                         1.0f);
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Arc) {
            const auto* arc_geo = std::get_if<editor::presentation::ArcGeometry>(&object.geometry);
            float cx = object.bounds.x + object.bounds.w * 0.5f + (arc_geo ? arc_geo->cx : 0.0f);
            float cy = object.bounds.y + object.bounds.h * 0.5f + (arc_geo ? arc_geo->cy : 0.0f);
            float radius = (arc_geo ? arc_geo->radius : 0.0f) * ctx.zoom;
            Pt center = ctx.world_to_screen(Pt(pos.x + cx, pos.y + cy));
            dl->add_rect(Pt(center.x - radius, center.y - radius),
                         Pt(center.x + radius, center.y + radius),
                         DEBUG_PAINT_BOUNDS_COLOR,
                         1.0f);
        }
    }
}

} // namespace visual
