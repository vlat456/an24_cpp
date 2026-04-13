#include "visual_node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "editor/layout_constants.h"
#include "visual/node/bounds.h"
#include "visual/container/linear_layout.h"
#include "visual/snap.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace visual {

// ============================================================================
// Construction
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
                        : static_cast<float>(text_.size()) * kFontSize * 0.6f;
        }
        return Pt(kPadding * 2.0f + text_w, kHeight);
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
        dl->add_text(text_pos, text_.c_str(), render_theme::COLOR_TEXT, font);
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
                        : static_cast<float>(text_.size()) * kFontSize * 0.6f;
        }
        return Pt(text_w + kRightPadding, kHeight);
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl || text_.empty()) return;

        Pt origin = ctx.world_to_screen(worldPos());
        float zoom = ctx.zoom;
        float font = kFontSize * zoom;
        Pt text_size = dl->calc_text_size(text_.c_str(), font);
        float tx = origin.x + size().x * zoom - text_size.x - kRightPadding * zoom;
        float ty = origin.y + (kHeight * zoom - font) * 0.5f;
        dl->add_text(Pt(tx, ty), text_.c_str(), render_theme::COLOR_TEXT_DIM, font);
    }

private:
    std::string text_;

    static constexpr float kHeight = 16.0f;
    static constexpr float kFontSize = 9.0f;
    static constexpr float kRightPadding = 5.0f;
};

constexpr float SWITCH_WIDTH = 48.0f;
constexpr float SWITCH_HEIGHT = 20.0f;
constexpr float VERTICAL_TOGGLE_WIDTH = 16.0f;
constexpr float VERTICAL_TOGGLE_HEIGHT = 48.0f;
constexpr float SLIDER_HEIGHT = 16.0f;
constexpr float SLIDER_MIN_WIDTH = 60.0f;
constexpr float SLIDER_TRACK_HEIGHT = 4.0f;
constexpr float SLIDER_HANDLE_RADIUS = 6.0f;
constexpr float INDICATOR_SIZE = 24.0f;
constexpr float KNOB_SIZE = 48.0f;
constexpr float KNOB_RADIUS = 16.0f;
constexpr float KNOB_TICK_INNER = 20.0f;
constexpr float KNOB_TICK_OUTER = 24.0f;
constexpr float KNOB_ARC_START_DEG = 225.0f;
constexpr float KNOB_ARC_SWEEP_DEG = -270.0f;
constexpr float GAUGE_RADIUS = 40.0f;
constexpr float GAUGE_NEEDLE_LENGTH = 32.0f;
constexpr float GAUGE_START_ANGLE = 210.0f;
constexpr float GAUGE_SWEEP_ANGLE = -240.0f;

enum class ContentInteractionRole {
    Toggle,
    DiscreteSelector,
    ContinuousScalar,
};

struct ContentWidgetInteractionInfo {
    ContentInteractionRole role;
    float primary_min = 0.0f;
    float primary_max = 100.0f;
    int steps = 2;
    float bounds_x = 0.0f;
    float bounds_y = 0.0f;
    float bounds_w = 0.0f;
    float bounds_h = 0.0f;
};

editor::presentation::SceneObjectId next_scene_object_id(
    editor::presentation::SemanticSceneSnapshot& snapshot) {
    return editor::presentation::SceneObjectId(
        static_cast<uint32_t>(snapshot.render_objects.size() + snapshot.hit_objects.size() + 1));
}

ContentWidgetInteractionInfo build_rect_interaction(ContentInteractionRole role,
                                                    Pt size,
                                                    float primary_min = 0.0f,
                                                    float primary_max = 100.0f,
                                                    int steps = 2) {
    return ContentWidgetInteractionInfo{
        .role = role,
        .primary_min = primary_min,
        .primary_max = primary_max,
        .steps = steps,
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = size.x,
        .bounds_h = size.y,
    };
}

Pt preferred_content_size(bp2::NodeContentType content_type) {
    switch (content_type) {
        case bp2::NodeContentType::Gauge:
            return Pt(GAUGE_RADIUS * 2.0f, GAUGE_RADIUS * 2.0f + 24.0f);
        case bp2::NodeContentType::Switch:
            return Pt(SWITCH_WIDTH, SWITCH_HEIGHT);
        case bp2::NodeContentType::VerticalToggle:
            return Pt(VERTICAL_TOGGLE_WIDTH, VERTICAL_TOGGLE_HEIGHT);
        case bp2::NodeContentType::Slider:
            return Pt(SLIDER_MIN_WIDTH, SLIDER_HEIGHT);
        case bp2::NodeContentType::Indicator:
            return Pt(INDICATOR_SIZE, INDICATOR_SIZE);
        case bp2::NodeContentType::Knob:
            return Pt(KNOB_SIZE, KNOB_SIZE);
        case bp2::NodeContentType::Text:
            return Pt(60.0f, 12.0f);
        default:
            return Pt(0.0f, 0.0f);
    }
}

std::optional<ContentWidgetInteractionInfo> derive_content_interaction(
    bp2::NodeContentType content_type, Pt size, float content_max) {
    if (size.x <= 0.0f || size.y <= 0.0f) return std::nullopt;
    
    switch (content_type) {
        case bp2::NodeContentType::Switch:
        case bp2::NodeContentType::VerticalToggle:
            return build_rect_interaction(ContentInteractionRole::Toggle, size);
        
        case bp2::NodeContentType::Slider: {
            float pad = SLIDER_HANDLE_RADIUS;
            float track_w = size.x - 2.0f * pad;
            return build_rect_interaction(ContentInteractionRole::ContinuousScalar,
                                          size, pad, pad + track_w);
        }
        
        case bp2::NodeContentType::Knob:
            return build_rect_interaction(ContentInteractionRole::DiscreteSelector,
                                          size, 0.0f, 100.0f,
                                          std::max(2, static_cast<int>(content_max)));
        
        default:
            return std::nullopt;
    }
}

void append_text_content_render_object(editor::presentation::SemanticSceneSnapshot& snapshot,
                                       ui::InternedId node_id,
                                       const Bounds& bounds,
                                       const std::string& label) {
    using namespace editor::presentation;

    if (label.empty()) {
        return;
    }

    SceneRenderObject render_object;
    render_object.id = next_scene_object_id(snapshot);
    render_object.node_id = node_id;
    render_object.element_id = node_id;
    render_object.kind = SceneRenderObjectKind::ContentPaint;
    render_object.primitive = PaintPrimitiveKind::Text;
    render_object.bounds = Rect{bounds.x, bounds.y, bounds.w, bounds.h};
    render_object.text = label;
    snapshot.render_objects.push_back(std::move(render_object));
}

void append_switch_content_render_objects(editor::presentation::SemanticSceneSnapshot& snapshot,
                                          ui::InternedId node_id,
                                          const Bounds& bounds,
                                          bool state,
                                          bool tripped,
                                          bool vertical) {
    using namespace editor::presentation;

    SceneRenderObject body;
    body.id = next_scene_object_id(snapshot);
    body.node_id = node_id;
    body.element_id = node_id;
    body.kind = SceneRenderObjectKind::ContentPaint;
    body.primitive = PaintPrimitiveKind::Rectangle;
    body.bounds = Rect{bounds.x, bounds.y, bounds.w, bounds.h};
    body.fill_color = tripped
        ? render_theme::COLOR_TRIPPED
        : (state ? 0xFF3A6830 : 0xFF1C1D24);
    body.stroke_color = render_theme::COLOR_BUS_BORDER;
    body.stroke_width = 1.0f;
    snapshot.render_objects.push_back(std::move(body));

    SceneRenderObject handle;
    handle.id = next_scene_object_id(snapshot);
    handle.node_id = node_id;
    handle.element_id = node_id;
    handle.kind = SceneRenderObjectKind::ContentPaint;
    handle.primitive = PaintPrimitiveKind::Rectangle;
    if (vertical) {
        const float handle_h = bounds.h * 0.24f;
        const float handle_y = state ? bounds.y + bounds.h * 0.15f : bounds.y + bounds.h * 0.70f;
        handle.bounds = Rect{bounds.x, handle_y, bounds.w, handle_h};
    } else {
        const float handle_w = bounds.w * 0.40f;
        const float handle_x = state ? bounds.x + bounds.w - handle_w : bounds.x;
        handle.bounds = Rect{handle_x, bounds.y, handle_w, bounds.h};
    }
    handle.fill_color = tripped
        ? render_theme::COLOR_TRIPPED
        : (state ? 0xFF3A6830 : 0xFF2C3038);
    handle.stroke_color = 0xFF1C1D24;
    handle.stroke_width = 1.0f;
    snapshot.render_objects.push_back(std::move(handle));
}

void append_slider_content_render_objects(editor::presentation::SemanticSceneSnapshot& snapshot,
                                          ui::InternedId node_id,
                                          const Bounds& bounds,
                                          float value,
                                          float min_val,
                                          float max_val) {
    using namespace editor::presentation;

    const float pad = SLIDER_HANDLE_RADIUS;
    const float track_h = SLIDER_TRACK_HEIGHT;
    const float track_y = bounds.y + (bounds.h - track_h) * 0.5f;
    const float track_w = std::max(0.0f, bounds.w - 2.0f * pad);
    const float range = max_val - min_val;
    const float t = (range > 1e-6f) ? std::clamp((value - min_val) / range, 0.0f, 1.0f) : 0.0f;

    SceneRenderObject track;
    track.id = next_scene_object_id(snapshot);
    track.node_id = node_id;
    track.element_id = node_id;
    track.kind = SceneRenderObjectKind::ContentPaint;
    track.primitive = PaintPrimitiveKind::Rectangle;
    track.bounds = Rect{bounds.x + pad, track_y, track_w, track_h};
    track.fill_color = 0xFF1C1D24;
    snapshot.render_objects.push_back(std::move(track));

    SceneRenderObject fill;
    fill.id = next_scene_object_id(snapshot);
    fill.node_id = node_id;
    fill.element_id = node_id;
    fill.kind = SceneRenderObjectKind::ContentPaint;
    fill.primitive = PaintPrimitiveKind::Rectangle;
    fill.bounds = Rect{bounds.x + pad, track_y, t * track_w, track_h};
    fill.fill_color = 0xFF3A6830;
    snapshot.render_objects.push_back(std::move(fill));

    SceneRenderObject handle;
    handle.id = next_scene_object_id(snapshot);
    handle.node_id = node_id;
    handle.element_id = node_id;
    handle.kind = SceneRenderObjectKind::ContentPaint;
    handle.primitive = PaintPrimitiveKind::Circle;
    handle.bounds = Rect{bounds.x + pad + t * track_w, bounds.y + bounds.h * 0.5f,
                         SLIDER_HANDLE_RADIUS, 16.0f};
    handle.fill_color = 0xFF5078C0;
    handle.stroke_color = 0xFF3050A0;
    handle.stroke_width = 1.0f;
    snapshot.render_objects.push_back(std::move(handle));

    SceneRenderObject label;
    label.id = next_scene_object_id(snapshot);
    label.node_id = node_id;
    label.element_id = node_id;
    label.kind = SceneRenderObjectKind::ContentPaint;
    label.primitive = PaintPrimitiveKind::Text;
    label.bounds = Rect{bounds.x, track_y + track_h + 1.0f, bounds.w, SLIDER_HEIGHT};
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", value);
    label.text = buf;
    snapshot.render_objects.push_back(std::move(label));
}

void append_indicator_content_render_objects(editor::presentation::SemanticSceneSnapshot& snapshot,
                                             ui::InternedId node_id,
                                             const Bounds& bounds,
                                             float brightness) {
    using namespace editor::presentation;
    SceneRenderObject indicator;
    indicator.id = next_scene_object_id(snapshot);
    indicator.node_id = node_id;
    indicator.element_id = node_id;
    indicator.kind = SceneRenderObjectKind::ContentPaint;
    indicator.primitive = PaintPrimitiveKind::Circle;
    indicator.bounds = Rect{bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f,
                            INDICATOR_SIZE * (0.3f + 0.15f * std::clamp(brightness, 0.0f, 1.0f)), 16.0f};
    uint32_t fill_color;
    if (brightness <= 0.0f) {
        fill_color = 0xFF505050;
    } else {
        float b = std::clamp(brightness, 0.0f, 1.0f);
        uint8_t g = static_cast<uint8_t>(48 + 207 * b);
        uint8_t r_col = static_cast<uint8_t>(48 * (1.0f - b));
        uint8_t b_col = static_cast<uint8_t>(48 * (1.0f - b));
        uint8_t alpha = static_cast<uint8_t>(80 + 175 * b);
        fill_color = (alpha << 24) | (b_col << 16) | (g << 8) | r_col;
    }
    indicator.fill_color = fill_color;
    indicator.stroke_color = 0xFF404040;
    indicator.stroke_width = 1.0f;
    snapshot.render_objects.push_back(std::move(indicator));
}

void append_knob_content_render_objects(editor::presentation::SemanticSceneSnapshot& snapshot,
                                        ui::InternedId node_id,
                                        const Bounds& bounds,
                                        int position,
                                        int num_positions) {
    using namespace editor::presentation;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    SceneRenderObject knob;
    knob.id = next_scene_object_id(snapshot);
    knob.node_id = node_id;
    knob.element_id = node_id;
    knob.kind = SceneRenderObjectKind::ContentPaint;
    knob.primitive = PaintPrimitiveKind::Circle;
    knob.bounds = Rect{cx, cy, KNOB_RADIUS, 24.0f};
    knob.fill_color = 0xFF3A3A42;
    knob.stroke_color = 0xFF606068;
    knob.stroke_width = 1.0f;
    snapshot.render_objects.push_back(std::move(knob));

    for (int i = 0; i < num_positions; ++i) {
        const float t = (num_positions > 1) ? static_cast<float>(i) / (num_positions - 1) : 0.5f;
        const float angle = KNOB_ARC_START_DEG + t * KNOB_ARC_SWEEP_DEG;
        SceneRenderObject tick;
        tick.id = next_scene_object_id(snapshot);
        tick.node_id = node_id;
        tick.element_id = node_id;
        tick.kind = SceneRenderObjectKind::ContentPaint;
        tick.primitive = PaintPrimitiveKind::Line;
        tick.bounds = Rect{cx, cy, angle, KNOB_TICK_OUTER};
        tick.fill_color = (i == position) ? 0xFF5078C0 : 0xFF808090;
        tick.inset = KNOB_TICK_INNER;
        tick.stroke_width = (i == position) ? 2.5f : 1.5f;
        snapshot.render_objects.push_back(std::move(tick));
    }

    const float sel_t = (num_positions > 1) ? static_cast<float>(position) / (num_positions - 1) : 0.5f;
    const float sel_angle = KNOB_ARC_START_DEG + sel_t * KNOB_ARC_SWEEP_DEG;
    SceneRenderObject indicator;
    indicator.id = next_scene_object_id(snapshot);
    indicator.node_id = node_id;
    indicator.element_id = node_id;
    indicator.kind = SceneRenderObjectKind::ContentPaint;
    indicator.primitive = PaintPrimitiveKind::Line;
    indicator.bounds = Rect{cx, cy, sel_angle, KNOB_RADIUS * 0.85f};
    indicator.fill_color = 0xFF5078C0;
    indicator.stroke_width = 2.0f;
    snapshot.render_objects.push_back(std::move(indicator));
}

void append_gauge_content_render_objects(editor::presentation::SemanticSceneSnapshot& snapshot,
                                         ui::InternedId node_id,
                                         const Bounds& bounds,
                                         float value,
                                         float min_val,
                                         float max_val,
                                         const std::string& unit) {
    using namespace editor::presentation;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + GAUGE_RADIUS;

    SceneRenderObject arc;
    arc.id = next_scene_object_id(snapshot);
    arc.node_id = node_id;
    arc.element_id = node_id;
    arc.kind = SceneRenderObjectKind::ContentPaint;
    arc.primitive = PaintPrimitiveKind::Arc;
    arc.bounds = Rect{cx, cy, GAUGE_RADIUS, GAUGE_SWEEP_ANGLE};
    arc.fill_color = 0xFF3E3130;
    arc.inset = GAUGE_START_ANGLE;
    arc.stroke_width = 2.0f;
    snapshot.render_objects.push_back(std::move(arc));

    const float range = max_val - min_val;
    const float normalized = (range > 1e-6f) ? std::clamp((value - min_val) / range, 0.0f, 1.0f) : 0.0f;
    const float needle_angle = GAUGE_START_ANGLE + normalized * GAUGE_SWEEP_ANGLE;
    SceneRenderObject needle;
    needle.id = next_scene_object_id(snapshot);
    needle.node_id = node_id;
    needle.element_id = node_id;
    needle.kind = SceneRenderObjectKind::ContentPaint;
    needle.primitive = PaintPrimitiveKind::Line;
    needle.bounds = Rect{cx, cy, needle_angle, GAUGE_NEEDLE_LENGTH};
    needle.fill_color = 0xFF2A70C8;
    needle.stroke_width = 2.0f;
    snapshot.render_objects.push_back(std::move(needle));

    SceneRenderObject value_text;
    value_text.id = next_scene_object_id(snapshot);
    value_text.node_id = node_id;
    value_text.element_id = node_id;
    value_text.kind = SceneRenderObjectKind::ContentPaint;
    value_text.primitive = PaintPrimitiveKind::Text;
    value_text.bounds = Rect{bounds.x, bounds.y + GAUGE_RADIUS * 2.0f + 5.0f,
                             bounds.w, 14.0f};
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", value);
    value_text.text = buf;
    snapshot.render_objects.push_back(std::move(value_text));

    SceneRenderObject unit_text;
    unit_text.id = next_scene_object_id(snapshot);
    unit_text.node_id = node_id;
    unit_text.element_id = node_id;
    unit_text.kind = SceneRenderObjectKind::ContentPaint;
    unit_text.primitive = PaintPrimitiveKind::Text;
    unit_text.bounds = Rect{bounds.x, bounds.y + GAUGE_RADIUS * 2.0f + 21.0f,
                            bounds.w, 10.0f};
    unit_text.text = unit;
    snapshot.render_objects.push_back(std::move(unit_text));
}

} // namespace

NodeWidget::NodeWidget(const bp2::Blueprint::Node& data,
                       const bp2::Interface& render_iface,
                       const ui::StringInterner& interner)
    : node_iid_(data.semantic.id)
    , interner_(&interner)
    , name_(data.view.name)
    , type_name_(std::string(interner.resolve(data.semantic.type)))
{
    if (data.view.has_color) {
        NodeColor c;
        c.r = data.view.color_r;
        c.g = data.view.color_g;
        c.b = data.view.color_b;
        c.a = data.view.color_a;
        custom_fill_ = c.to_uint32();
    }

    setLocalPos(Pt(data.layout.x, data.layout.y));
    buildLayout(data, render_iface, interner);

    // Auto-size: compute preferred, snap to grid
    Pt preferred = preferredSize(nullptr);

    float w = preferred.x;
    float h = preferred.y;

    bool has_explicit = data.layout.width.has_value() && data.layout.height.has_value();
    if (has_explicit) {
        // Trust the user's explicit size — only enforce a hard minimum
        // (PORT_LAYOUT_GRID) to prevent degenerate zero-area nodes.
        if (*data.layout.width >= editor_constants::PORT_LAYOUT_GRID) w = *data.layout.width;
        if (*data.layout.height >= editor_constants::PORT_LAYOUT_GRID) h = *data.layout.height;
    }
    spdlog::debug("[widget] NodeWidget layout: node='{}' type='{}' preferred=({},{}) explicit_size={} final=({},{})",
                  data.view.name, type_name_, preferred.x, preferred.y,
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
            // Parse string to bp2::PortLayoutSide
            lo.side = bp2::parse_port_layout_side(*ov.side);
        }
        if (ov.position.has_value()) {
            lo.position = static_cast<uint8_t>(*ov.position);
        }
        result.push_back(std::move(lo));
    }
    return result;
}

void NodeWidget::buildLayout(const bp2::Blueprint::Node& data,
                              const bp2::Interface& render_iface,
                              const ui::StringInterner& interner) {
    layout_ = emplaceChild<Column>();

    // -- Header --
    layout_->emplaceChild<HeaderStrip>(name_);

    bp2::NodeContentType content_type = data.view.content_type;
    cached_content_type_ = content_type;
    cached_content_min_ = data.view.content_min;
    cached_content_max_ = data.view.content_max;
    cached_content_value_ = data.view.content_value;
    cached_content_label_ = data.view.content_label;
    cached_content_state_ = data.view.content_state;
    cached_content_tripped_ = data.view.content_tripped;
    cached_content_unit_ = data.view.content_unit;

    // -- Port rows / Content --
    // VerticalToggle uses special layout, but falls back to standard when overrides present
    if (content_type == bp2::NodeContentType::VerticalToggle && data.layout.layout_overrides.empty()) {
        buildVerticalToggleLayout(data, render_iface, interner);
    } else {
        buildStandardLayout(data, render_iface, interner);
    }

    // -- Flex spacer pushes footer to bottom when node is resized taller.
    //    Only added when no other flex child exists (e.g., pure port-only nodes),
    //    otherwise the content flex child handles the stretching. --
    if (content_preferred_size_.x <= 0.0f && content_preferred_size_.y <= 0.0f) {
        layout_->emplaceChild<Spacer>();
    }

    // -- Type name footer --
    layout_->emplaceChild<FooterTypeLabel>(type_name_);
}

void NodeWidget::configure_content_geometry(bp2::NodeContentType content_type) {
    content_preferred_size_ = preferred_content_size(content_type);
}

void NodeWidget::buildStandardLayout(const bp2::Blueprint::Node& data,
                                     const bp2::Interface& render_iface,
                                     const ui::StringInterner& interner) {
    bp2::NodeContentType content_type = data.view.content_type;
    const std::vector<bp2::NodePort> input_ports = bp2::derive_input_ports(render_iface);
    const std::vector<bp2::NodePort> output_ports = bp2::derive_output_ports(render_iface);

    // Fast path: no overrides — use existing paired-row layout
    if (data.layout.layout_overrides.empty()) {
        // Port rows: pair inputs and outputs.
        // [BUG-2] InOut ports appear in BOTH inputs and outputs arrays;
        // filter duplicates from outputs so they only render on the left side.
        std::vector<bp2::NodePort> right_ports;
        right_ports.reserve(output_ports.size());
        for (const auto& p : output_ports) {
            if (p.side == bp2::PortSide::InOut) continue;  // already in inputs
            right_ports.push_back(p);
        }

        size_t max_ports = std::max(input_ports.size(), right_ports.size());
        for (size_t i = 0; i < max_ports; i++) {
            std::string_view left_name;
            std::string_view right_name;
            if (i < input_ports.size()) {
                left_name = interner.resolve(input_ports[i].name);
            }
            PortType left_type = (i < input_ports.size()) ? input_ports[i].type : PortType::Any;
            if (i < right_ports.size()) {
                right_name = interner.resolve(right_ports[i].name);
            }
            PortType right_type = (i < right_ports.size()) ? right_ports[i].type : PortType::Any;
            buildPortRow(left_name, left_type, right_name, right_type);
        }

        // Content area (appended below port rows in the root Column)
        if (content_type == bp2::NodeContentType::Gauge) {
            configure_content_geometry(content_type);
            layout_->emplaceChild<Spacer>()->setFlexible(false);
        } else if (content_type == bp2::NodeContentType::Switch) {
            auto* container = layout_->emplaceChild<Container>(Edges{0, 0, 0, 0});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        } else if (content_type == bp2::NodeContentType::VerticalToggle) {
            auto* container = layout_->emplaceChild<Container>(Edges{0, 0, 0, 0});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        } else if (content_type == bp2::NodeContentType::Slider) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            float v_pad = 2.0f;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, v_pad, margin, v_pad});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        } else if (content_type == bp2::NodeContentType::Indicator) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 2.0f, margin, 2.0f});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        } else if (content_type == bp2::NodeContentType::Knob) {
            float margin = PortConstants::RADIUS + PortConstants::LEFT_LABEL_OFFSET;
            auto* container = layout_->emplaceChild<Container>(
                Edges{margin, 2.0f, margin, 2.0f});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        } else if (content_type != bp2::NodeContentType::None) {
            auto* container = layout_->emplaceChild<Container>(Edges{0, 0, 0, 0});
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<Spacer>()->setSize(content_preferred_size_);
        }
    } else {
        // Slow path: four-sided layout with overrides.
        // Content is placed inside the center column of the body row.
        buildFourSidedLayout(data, render_iface, interner);
    }
}

void NodeWidget::buildVerticalToggleLayout(const bp2::Blueprint::Node& data,
                                           const bp2::Interface& render_iface,
                                           const ui::StringInterner& interner) {
    auto* main_row = layout_->emplaceChild<Row>();
    const std::vector<bp2::NodePort> input_ports = bp2::derive_input_ports(render_iface);
    const std::vector<bp2::NodePort> output_ports = bp2::derive_output_ports(render_iface);

    // Left column (input ports)
    auto* left_col = main_row->emplaceChild<Column>();
    for (const auto& p : input_ports) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(left_col, name_sv, p.type, bp2::PortSide::Input, bp2::PortLayoutSide::Left);
    }

    // Center column (vertical toggle) — flex to push right column to the edge
    auto* center_col = main_row->emplaceChild<Column>();
    center_col->setFlexGrow(1.0f);
    auto* toggle_container = center_col->emplaceChild<Container>(
        Edges{0, 5.0f, 0, 5.0f});
    configure_content_geometry(bp2::NodeContentType::VerticalToggle);
    toggle_container->emplaceChild<Spacer>()->setSize(content_preferred_size_);

    // Right column (output ports)
    auto* right_col = main_row->emplaceChild<Column>();
    for (const auto& p : output_ports) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(right_col, name_sv, p.type, bp2::PortSide::Output, bp2::PortLayoutSide::Right);
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
                                   PortType type, bp2::PortSide logical_side, bp2::PortLayoutSide layout_side) {
    auto* row = col->emplaceChild<PortRow>(name, logical_side, type, layout_side, &layout_ctx_);
    if (row->port()) ports_.push_back(row->port());
}

void NodeWidget::buildFourSidedLayout(const bp2::Blueprint::Node& data,
                                      const bp2::Interface& render_iface,
                                      const ui::StringInterner& interner) {
    using namespace editor_constants;

    auto overrides = resolve_bp2_layout_overrides(data.layout.layout_overrides);
    const std::vector<bp2::NodePort> input_ports = bp2::derive_input_ports(render_iface);
    const std::vector<bp2::NodePort> output_ports = bp2::derive_output_ports(render_iface);
    ResolvedLayout layout = resolve_port_layout(input_ports, output_ports,
                                                overrides, interner);
    
    bp2::NodeContentType content_type = data.view.content_type;

    // Top port strip
    if (!layout.top.empty()) {
        buildHorizontalPortStrip(layout.top);
    }
    
    // Main body row: [Left ports | Content | Right ports]
    auto* body_row = layout_->emplaceChild<Row>();
    
    // Left column (input ports that stay on left)
    auto* left_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.left) {
        buildPortInColumn(left_col, rp.port_name, rp.type, rp.logical_side, bp2::PortLayoutSide::Left);
    }
    
    // Center column: content widget or spacer.
    // Must be flexible so it absorbs remaining width, pushing right_col to
    // the node's right edge (mirroring buildVerticalToggleLayout).
    auto* center = body_row->emplaceChild<Container>(Edges{4, 0, 4, 0});
    center->setFlexGrow(1.0f);

    if (content_type == bp2::NodeContentType::Gauge) {
        configure_content_geometry(content_type);
        center->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type == bp2::NodeContentType::Switch) {
        configure_content_geometry(content_type);
        center->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type == bp2::NodeContentType::VerticalToggle) {
        configure_content_geometry(content_type);
        center->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type == bp2::NodeContentType::Slider) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        configure_content_geometry(content_type);
        inner->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type == bp2::NodeContentType::Indicator) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        configure_content_geometry(content_type);
        inner->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type == bp2::NodeContentType::Knob) {
        auto* inner = center->emplaceChild<Container>(Edges{0, 2.0f, 0, 2.0f});
        configure_content_geometry(content_type);
        inner->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else if (content_type != bp2::NodeContentType::None) {
        configure_content_geometry(content_type);
        center->emplaceChild<Spacer>()->setSize(content_preferred_size_);
    } else {
        center->emplaceChild<Spacer>();
    }
    
    // Right column (output ports that stay on right)
    auto* right_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.right) {
        buildPortInColumn(right_col, rp.port_name, rp.type, rp.logical_side, bp2::PortLayoutSide::Right);
    }
    
    // Bottom port strip
    if (!layout.bottom.empty()) {
        buildHorizontalPortStrip(layout.bottom);
    }
}

void NodeWidget::buildHorizontalPortStrip(const std::vector<ResolvedPort>& ports) {
    if (ports.empty()) return;

    bp2::PortLayoutSide side = ports[0].layout_side;
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
    cached_content_min_ = content.min;
    cached_content_max_ = content.max;
    cached_content_value_ = content.value;
    cached_content_label_ = content.label;
    cached_content_state_ = content.state;
    cached_content_tripped_ = content.tripped;
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

    // -- Render objects: produce paint primitives for content types rendered
    //    from the semantic snapshot (replacing the former widget rendering). --
    switch (cached_content_type_) {
        case bp2::NodeContentType::Text:
            if (!cached_content_label_.empty()) {
                append_text_content_render_object(content_semantic_snapshot_, node_iid_, cb, cached_content_label_);
                render_content_from_semantic_snapshot_ = true;
            }
            break;
        case bp2::NodeContentType::Switch:
            append_switch_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                                 cached_content_state_, cached_content_tripped_, false);
            render_content_from_semantic_snapshot_ = true;
            break;
        case bp2::NodeContentType::VerticalToggle:
            append_switch_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                                 cached_content_state_, cached_content_tripped_, true);
            render_content_from_semantic_snapshot_ = true;
            break;
        case bp2::NodeContentType::Slider:
            append_slider_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                                 cached_content_value_, cached_content_min_, cached_content_max_);
            render_content_from_semantic_snapshot_ = true;
            break;
        case bp2::NodeContentType::Indicator:
            append_indicator_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                                    cached_content_value_);
            render_content_from_semantic_snapshot_ = true;
            break;
        case bp2::NodeContentType::Knob:
            append_knob_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                               static_cast<int>(cached_content_value_),
                                               std::max(2, static_cast<int>(cached_content_max_)));
            render_content_from_semantic_snapshot_ = true;
            break;
        case bp2::NodeContentType::Gauge:
            append_gauge_content_render_objects(content_semantic_snapshot_, node_iid_, cb,
                                                cached_content_value_, cached_content_min_, cached_content_max_,
                                                cached_content_unit_);
            render_content_from_semantic_snapshot_ = true;
            break;
        default:
            break;
    }

    // -- Hit objects: produce interaction regions for all interactive content types. --
    auto interaction_info = derive_content_interaction(cached_content_type_, content_preferred_size_, cached_content_max_);
    if (!interaction_info.has_value()) {
        return;
    }

    using namespace editor::presentation;

    SceneHitObject hit_object;
    hit_object.id = next_scene_object_id(content_semantic_snapshot_);
    hit_object.node_id = node_iid_;
    hit_object.element_id = node_iid_;
    hit_object.region_id = node_iid_;
    hit_object.kind = SceneHitObjectKind::ContentRegion;
    hit_object.shape = HitShapeKind::Rectangle;
    hit_object.bounds = Rect{
        worldPos().x + cb.x + interaction_info->bounds_x,
        worldPos().y + cb.y + interaction_info->bounds_y,
        interaction_info->bounds_w,
        interaction_info->bounds_h,
    };

    InteractionBinding binding;
    binding.region_id = node_iid_;
    binding.action_id = node_iid_;
    switch (interaction_info->role) {
        case ContentInteractionRole::ContinuousScalar:
            binding.kind = InteractionKind::DragScalar;
            binding.min_value = interaction_info->primary_min;
            binding.max_value = interaction_info->primary_max;
            break;
        case ContentInteractionRole::DiscreteSelector:
            binding.kind = InteractionKind::DragDiscrete;
            binding.min_value = interaction_info->primary_min;
            binding.max_value = interaction_info->primary_max;
            binding.step = static_cast<float>(interaction_info->steps);
            break;
        case ContentInteractionRole::Toggle:
            binding.kind = InteractionKind::Click;
            break;
    }
    hit_object.interactions.push_back(std::move(binding));

    content_semantic_snapshot_.hit_objects.push_back(std::move(hit_object));
}

NodeVisualState NodeWidget::visual_state(const RenderContext& ctx) const {
    NodeVisualState state;
    state.selected = ctx.isNodeSelected(this);
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
    
    // When content lives under a flexible ancestor inside a linear layout,
    // the flexible child contributes 0 on that layout's main axis. Reserve the
    // content widget's intrinsic size on the zeroed axis so fixed-affordance
    // controls do not lose their visible/hittable area inside the old layout system.
    if (content_preferred_size_.x > 0.0f || content_preferred_size_.y > 0.0f) {
        if (content_preferred_size_.x > 0) {
            ps.x = std::max(ps.x, content_preferred_size_.x + 8.0f);
        }
        if (content_preferred_size_.y > 0) {
            ps.y += content_preferred_size_.y;
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

    content_bounds_ = {};
    if (content_preferred_size_.x > 0.0f || content_preferred_size_.y > 0.0f) {
        content_bounds_ = Bounds{
            std::max(0.0f, (w - content_preferred_size_.x) * 0.5f),
            std::max(0.0f, (h - content_preferred_size_.y) * 0.5f),
            content_preferred_size_.x,
            content_preferred_size_.y,
        };
    }

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
                Pt text_pos = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
                dl->add_text(text_pos, object.text.c_str(), render_theme::COLOR_TEXT_DIM, 10.0f * ctx.zoom);
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Rectangle) {
                Pt min = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
                Pt max = ctx.world_to_screen(Pt(pos.x + object.bounds.x + object.bounds.w,
                                                pos.y + object.bounds.y + object.bounds.h));
                dl->add_rect_filled(min, max, object.fill_color);
                if (object.stroke_width > 0.0f) {
                    dl->add_rect(min, max, object.stroke_color, object.stroke_width * ctx.zoom);
                }
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Circle) {
                Pt center = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
                float radius = object.bounds.w * ctx.zoom;
                dl->add_circle_filled(center, radius, object.fill_color, 24);
                if (object.stroke_width > 0.0f) {
                    dl->add_circle(center, radius, object.stroke_color, 24);
                }
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Line) {
                // Line encoding contract:
                //   bounds = {center_x, center_y, angle_degrees, end_radius}
                //   inset  = start_radius (0 = from center)
                //   stroke_width = line thickness
                //   fill_color   = line color
                constexpr float DEG2RAD = 3.14159265f / 180.0f;
                Pt center = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
                float angle = object.bounds.w * DEG2RAD;
                float radius_a = object.inset * ctx.zoom;
                float radius_b = object.bounds.h * ctx.zoom;
                Pt a(center.x + std::cos(angle) * radius_a, center.y - std::sin(angle) * radius_a);
                Pt b(center.x + std::cos(angle) * radius_b, center.y - std::sin(angle) * radius_b);
                dl->add_line(a, b, object.fill_color, object.stroke_width * ctx.zoom);
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Arc) {
                constexpr float DEG2RAD = 3.14159265f / 180.0f;
                Pt center = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
                float radius = object.bounds.w * ctx.zoom;
                float start_angle = object.inset * DEG2RAD;
                float sweep_angle = object.bounds.h * DEG2RAD;
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
