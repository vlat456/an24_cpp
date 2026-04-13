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
                        : fallback_text_width(text_, kFontSize);
        }
        return Pt(kPadding * 2.0f + text_w, kHeight);
    }

    Pt minimumSize(IDrawList* dl) const override {
        return preferredSize(dl);
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

    Pt minimumSize(IDrawList* dl) const override {
        return preferredSize(dl);
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

    void renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const override {
        if (!dl || text_.empty()) return;
        const float font = kFontSize * ctx.zoom;
        Pt origin = ctx.world_to_screen(worldPos());
        Pt text_size = dl->calc_text_size(text_.c_str(), font);
        float tx = origin.x + size().x * ctx.zoom - text_size.x - kRightPadding * ctx.zoom;
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
constexpr float GAUGE_VALUE_FONT_SIZE = 14.0f;
constexpr float GAUGE_UNIT_FONT_SIZE = 10.0f;
constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;
constexpr uint32_t COLOR_NEEDLE = 0xFF2A70C8;
constexpr uint32_t COLOR_TICK_MAJOR = 0xFFDCD5D4;
constexpr uint32_t COLOR_TICK_MINOR = 0xFF606070;
constexpr uint32_t COLOR_GAUGE_TEXT = 0xFFDCD5D4;
constexpr float CONTENT_MARGIN_X = 5.0f;
constexpr float CONTENT_MARGIN_Y = 5.0f;

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

/// Layout policy for content within its container cell.
/// Alignment values: 0.0 = start (left/top), 0.5 = center, 1.0 = end (right/bottom).
struct ContentLayoutPolicy {
    Pt preferred_size{0.0f, 0.0f};
    float align_x = 0.5f;   ///< Horizontal alignment within container
    float align_y = 0.5f;   ///< Vertical alignment within container
    bool reserve_width = true;
    bool reserve_height = true;
};

struct TextPaintGeometry {
    Pt pos;
    Pt size;
    float font = 0.0f;
};

struct LinePaintGeometry {
    Pt a;
    Pt b;
};

constexpr float DEG2RAD = 3.14159265f / 180.0f;

TextPaintGeometry resolve_text_paint_geometry(const editor::presentation::SceneRenderObject& object,
                                              const Pt& node_pos,
                                              IDrawList& dl,
                                              const RenderContext& ctx) {
    TextPaintGeometry geometry;
    geometry.font = (object.text_size > 0.0f ? object.text_size : 10.0f) * ctx.zoom;
    geometry.pos = ctx.world_to_screen(Pt(node_pos.x + object.bounds.x, node_pos.y + object.bounds.y));
    geometry.size = dl.calc_text_size(object.text.c_str(), geometry.font);
    if (object.bounds.w <= 0.0f && object.bounds.h <= 0.0f) {
        geometry.pos.x -= geometry.size.x * 0.5f;
    }
    return geometry;
}

LinePaintGeometry resolve_line_paint_geometry(const editor::presentation::SceneRenderObject& object,
                                              const Pt& node_pos,
                                              const RenderContext& ctx) {
    Pt center = ctx.world_to_screen(Pt(node_pos.x + object.bounds.x, node_pos.y + object.bounds.y));
    float angle = object.bounds.w * DEG2RAD;
    float radius_a = object.inset * ctx.zoom;
    float radius_b = object.bounds.h * ctx.zoom;
    return {
        Pt(center.x + std::cos(angle) * radius_a, center.y - std::sin(angle) * radius_a),
        Pt(center.x + std::cos(angle) * radius_b, center.y - std::sin(angle) * radius_b),
    };
}

/// Single authoritative table mapping content type → intrinsic size + alignment.
/// All per-type layout decisions live here; no type-checks needed downstream.
ContentLayoutPolicy content_layout_policy(bp2::NodeContentType content_type) {
    switch (content_type) {
        case bp2::NodeContentType::Gauge:
            return {Pt(GAUGE_RADIUS * 2.0f,
                       GAUGE_RADIUS * 2.0f + GAUGE_VALUE_FONT_SIZE + GAUGE_UNIT_FONT_SIZE + 10.0f),
                    0.5f, 0.5f, true, true};
        case bp2::NodeContentType::Switch:
            return {Pt(SWITCH_WIDTH, SWITCH_HEIGHT), 0.5f, 0.5f, true, true};
        case bp2::NodeContentType::VerticalToggle:
            // Draw at top of reserved area so it doesn't overlap the header
            return {Pt(VERTICAL_TOGGLE_WIDTH, VERTICAL_TOGGLE_HEIGHT), 0.5f, 0.0f, true, false};
        case bp2::NodeContentType::Slider:
            return {Pt(SLIDER_MIN_WIDTH, SLIDER_HEIGHT), 0.5f, 0.5f, true, true};
        case bp2::NodeContentType::Indicator:
            return {Pt(INDICATOR_SIZE, INDICATOR_SIZE), 0.5f, 0.5f, true, true};
        case bp2::NodeContentType::Knob:
            return {Pt(KNOB_SIZE, KNOB_SIZE), 0.5f, 0.5f, true, true};
        case bp2::NodeContentType::Text:
            return {Pt(60.0f, 12.0f), 0.5f, 0.5f, true, true};
        default:
            return {};
    }
}

ui::Edges standard_content_margins() {
    return ui::Edges{CONTENT_MARGIN_X, CONTENT_MARGIN_Y, CONTENT_MARGIN_X, CONTENT_MARGIN_Y};
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

struct ContentPresentationBuildResult {
    editor::presentation::NodePresentation presentation;
    editor::presentation::NodeSlotLayout layout;
    bool renders_content = false;
};

editor::presentation::PresentationNode make_presentation_node(ui::InternedId element_id) {
    editor::presentation::PresentationNode node;
    node.element_id = element_id;
    return node;
}

editor::presentation::PaintCommand make_paint_command(ui::InternedId id,
                                                      editor::presentation::PaintPrimitiveKind kind) {
    editor::presentation::PaintCommand paint;
    paint.id = id;
    paint.kind = kind;
    return paint;
}

editor::presentation::InteractionBinding make_interaction_binding(
    ui::InternedId region_id,
    ui::InternedId action_id,
    editor::presentation::InteractionKind kind) {
    editor::presentation::InteractionBinding binding;
    binding.region_id = region_id;
    binding.action_id = action_id;
    binding.kind = kind;
    return binding;
}

void append_placement(editor::presentation::NodeSlotLayout& layout,
                      ui::InternedId element_id,
                      float x,
                      float y,
                      float w,
                      float h) {
    layout.placements.push_back(editor::presentation::FragmentPlacement{
        element_id,
        editor::presentation::Rect{x, y, w, h},
    });
}

ContentPresentationBuildResult build_content_presentation(
    ui::InternedId node_id,
    const Bounds& bounds,
    bp2::NodeContentType content_type,
    float min_value,
    float max_value,
    float value,
    std::string_view label,
    bool state,
    bool tripped,
    std::string_view unit,
    const ContentWidgetInteractionInfo* interaction_info) {
    using namespace editor::presentation;

    // Element IDs for presentation nodes use a local counter starting at 1.
    // These IDs are internal to the snapshot and must not collide with each other.
    uint32_t next_id = 1;
    auto next_element_id = [&]() { return ui::InternedId(next_id++); };

    ContentPresentationBuildResult result;
    result.presentation.node_id = node_id;
    result.presentation.type_id = node_id;
    result.presentation.shell.frame_kind = NodeFrameKind::Standard;

    ui::InternedId root_element_id = next_element_id();
    result.presentation.content.root = make_presentation_node(root_element_id);
    result.presentation.content.root.layout = LayoutKind::Overlay;

    result.layout.node_bounds = Rect{0.0f, 0.0f, bounds.x + bounds.w, bounds.y + bounds.h};
    result.layout.slots.push_back(SlotAssignment{NodeSlot::Header, Rect{0.0f, 0.0f, bounds.x + bounds.w, 0.0f}});
    result.layout.slots.push_back(SlotAssignment{NodeSlot::Body, Rect{bounds.x, bounds.y, bounds.w, bounds.h}});

    // Root presentation node covers the full content bounds.
    append_placement(result.layout, root_element_id, bounds.x, bounds.y, bounds.w, bounds.h);

    auto append_painted = [&](PaintPrimitiveKind kind,
                              const Rect& placement,
                              auto configure) {
        PresentationNode node = make_presentation_node(next_element_id());
        PaintCommand paint = make_paint_command(next_element_id(), kind);
        configure(paint);
        node.paint.push_back(std::move(paint));
        append_placement(result.layout, node.element_id,
                         placement.x, placement.y, placement.w, placement.h);
        result.presentation.content.root.children.push_back(std::move(node));
        result.renders_content = true;
    };

    switch (content_type) {
        case bp2::NodeContentType::Text:
            if (!label.empty()) {
                append_painted(PaintPrimitiveKind::Text,
                               Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                               [&](PaintCommand& paint) {
                                   paint.text = std::string(label);
                               });
            }
            break;
        case bp2::NodeContentType::Switch:
        case bp2::NodeContentType::VerticalToggle: {
            const bool vertical = content_type == bp2::NodeContentType::VerticalToggle;
            append_painted(PaintPrimitiveKind::Rectangle,
                           Rect{bounds.x, bounds.y, bounds.w, bounds.h},
                           [&](PaintCommand& paint) {
                               paint.fill_color = tripped
                                   ? render_theme::COLOR_TRIPPED
                                   : (state ? 0xFF3A6830 : 0xFF1C1D24);
                               paint.stroke_color = render_theme::COLOR_BUS_BORDER;
                               paint.stroke_width = 1.0f;
                           });

            if (vertical) {
                const float handle_h = bounds.h * 0.24f;
                const float handle_y = state ? bounds.y + bounds.h * 0.15f : bounds.y + bounds.h * 0.70f;
                append_painted(PaintPrimitiveKind::Rectangle,
                               Rect{bounds.x, handle_y, bounds.w, handle_h},
                               [&](PaintCommand& paint) {
                                   paint.fill_color = tripped
                                       ? render_theme::COLOR_TRIPPED
                                       : (state ? 0xFF3A6830 : 0xFF2C3038);
                                   paint.stroke_color = 0xFF1C1D24;
                                   paint.stroke_width = 1.0f;
                               });
            } else {
                const float handle_w = bounds.w * 0.40f;
                const float handle_x = state ? bounds.x + bounds.w - handle_w : bounds.x;
                append_painted(PaintPrimitiveKind::Rectangle,
                               Rect{handle_x, bounds.y, handle_w, bounds.h},
                               [&](PaintCommand& paint) {
                                   paint.fill_color = tripped
                                       ? render_theme::COLOR_TRIPPED
                                       : (state ? 0xFF3A6830 : 0xFF2C3038);
                                   paint.stroke_color = 0xFF1C1D24;
                                   paint.stroke_width = 1.0f;
                               });
            }
            break;
        }
        case bp2::NodeContentType::Slider: {
            const float pad = SLIDER_HANDLE_RADIUS;
            const float track_h = SLIDER_TRACK_HEIGHT;
            const float track_y = bounds.y + (bounds.h - track_h) * 0.5f;
            const float track_w = std::max(0.0f, bounds.w - 2.0f * pad);
            const float range = max_value - min_value;
            const float t = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;

            append_painted(PaintPrimitiveKind::Rectangle,
                           Rect{bounds.x + pad, track_y, track_w, track_h},
                           [&](PaintCommand& paint) {
                               paint.fill_color = 0xFF1C1D24;
                           });
            append_painted(PaintPrimitiveKind::Rectangle,
                           Rect{bounds.x + pad, track_y, t * track_w, track_h},
                           [&](PaintCommand& paint) {
                               paint.fill_color = 0xFF3A6830;
                           });
            append_painted(PaintPrimitiveKind::Circle,
                           Rect{bounds.x + pad + t * track_w,
                                bounds.y + bounds.h * 0.5f,
                                SLIDER_HANDLE_RADIUS,
                                0.0f},
                           [&](PaintCommand& paint) {
                               paint.fill_color = 0xFF5078C0;
                               paint.stroke_color = 0xFF3050A0;
                               paint.stroke_width = 1.0f;
                           });
            append_painted(PaintPrimitiveKind::Text,
                           Rect{bounds.x, track_y + track_h + 1.0f, bounds.w, SLIDER_HEIGHT},
                           [&](PaintCommand& paint) {
                               char buf[32];
                               snprintf(buf, sizeof(buf), "%.1f", value);
                               paint.text = buf;
                           });
            break;
        }
        case bp2::NodeContentType::Indicator: {
            const float b = std::clamp(value, 0.0f, 1.0f);
            const float radius = INDICATOR_SIZE * (0.3f + 0.15f * b);
            const float cx = bounds.x + bounds.w * 0.5f;
            const float cy = bounds.y + bounds.h * 0.5f;
            append_painted(PaintPrimitiveKind::Circle,
                           Rect{cx, cy, radius, 0.0f},
                           [&](PaintCommand& paint) {
                                if (value <= 0.0f) {
                                    paint.fill_color = 0xFF505050;
                               } else {
                                   uint8_t g = static_cast<uint8_t>(48 + 207 * b);
                                   uint8_t r_col = static_cast<uint8_t>(48 * (1.0f - b));
                                   uint8_t b_col = static_cast<uint8_t>(48 * (1.0f - b));
                                   uint8_t alpha = static_cast<uint8_t>(80 + 175 * b);
                                   paint.fill_color = (alpha << 24) | (b_col << 16) | (g << 8) | r_col;
                               }
                               paint.stroke_color = 0xFF404040;
                               paint.stroke_width = 1.0f;
                           });
            break;
        }
        case bp2::NodeContentType::Knob: {
            const int num_positions = std::max(2, static_cast<int>(max_value));
            const int position = std::clamp(static_cast<int>(value), 0, num_positions - 1);
            const float cx = bounds.x + bounds.w * 0.5f;
            const float cy = bounds.y + bounds.h * 0.5f;
            append_painted(PaintPrimitiveKind::Circle,
                           Rect{cx, cy, KNOB_RADIUS, 0.0f},
                           [&](PaintCommand& paint) {
                                paint.fill_color = 0xFF3A3A42;
                                paint.stroke_color = 0xFF606068;
                               paint.stroke_width = 1.0f;
                           });

            for (int i = 0; i < num_positions; ++i) {
                const float t = (num_positions > 1) ? static_cast<float>(i) / (num_positions - 1) : 0.5f;
                const float angle = KNOB_ARC_START_DEG + t * KNOB_ARC_SWEEP_DEG;
                append_painted(PaintPrimitiveKind::Line,
                               Rect{cx, cy, angle, KNOB_TICK_OUTER},
                               [&](PaintCommand& paint) {
                                   paint.fill_color = (i == position) ? 0xFF5078C0 : 0xFF808090;
                                   paint.inset = KNOB_TICK_INNER;
                                   paint.stroke_width = (i == position) ? 2.5f : 1.5f;
                               });
            }

            const float sel_t = (num_positions > 1) ? static_cast<float>(position) / (num_positions - 1) : 0.5f;
            const float sel_angle = KNOB_ARC_START_DEG + sel_t * KNOB_ARC_SWEEP_DEG;
            append_painted(PaintPrimitiveKind::Line,
                           Rect{cx, cy, sel_angle, KNOB_RADIUS * 0.85f},
                           [&](PaintCommand& paint) {
                               paint.fill_color = 0xFF5078C0;
                               paint.stroke_width = 2.0f;
                           });
            break;
        }
        case bp2::NodeContentType::Gauge: {
            const float cx = bounds.x + bounds.w * 0.5f;
            const float cy = bounds.y + GAUGE_RADIUS;
            append_painted(PaintPrimitiveKind::Arc,
                           Rect{cx, cy, GAUGE_RADIUS, GAUGE_SWEEP_ANGLE},
                           [&](PaintCommand& paint) {
                               paint.fill_color = COLOR_GAUGE_BORDER;
                               paint.inset = GAUGE_START_ANGLE;
                               paint.stroke_width = 2.0f;
                           });

            for (int i = 0; i < 11; ++i) {
                const float t = static_cast<float>(i) / 10.0f;
                const float angle = GAUGE_START_ANGLE + t * GAUGE_SWEEP_ANGLE;
                const bool is_major = (i % 5) == 0;
                append_painted(PaintPrimitiveKind::Line,
                               Rect{cx, cy, angle, GAUGE_RADIUS},
                               [&](PaintCommand& paint) {
                                   paint.fill_color = is_major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR;
                                   paint.inset = GAUGE_RADIUS - (is_major ? 6.0f : 3.0f);
                                   paint.stroke_width = 1.5f;
                               });
            }

            const float range = max_value - min_value;
            const float normalized = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;
            const float needle_angle = GAUGE_START_ANGLE + normalized * GAUGE_SWEEP_ANGLE;
            append_painted(PaintPrimitiveKind::Line,
                           Rect{cx, cy, needle_angle, GAUGE_NEEDLE_LENGTH},
                           [&](PaintCommand& paint) {
                               paint.fill_color = COLOR_NEEDLE;
                               paint.stroke_width = 2.0f;
                           });
            append_painted(PaintPrimitiveKind::Circle,
                           Rect{cx, cy, 3.0f, 0.0f},
                           [&](PaintCommand& paint) {
                               paint.fill_color = COLOR_NEEDLE;
                           });
            append_painted(PaintPrimitiveKind::Text,
                           Rect{cx, bounds.y + GAUGE_RADIUS * 2.0f + 5.0f, 0.0f, 0.0f},
                           [&](PaintCommand& paint) {
                               char buf[32];
                               snprintf(buf, sizeof(buf), "%.1f", value);
                               paint.text = buf;
                               paint.fill_color = COLOR_GAUGE_TEXT;
                               paint.text_size = GAUGE_VALUE_FONT_SIZE;
                           });
            append_painted(PaintPrimitiveKind::Text,
                           Rect{cx, bounds.y + GAUGE_RADIUS * 2.0f + 21.0f, 0.0f, 0.0f},
                           [&](PaintCommand& paint) {
                                paint.text = std::string(unit);
                                paint.fill_color = render_theme::COLOR_TEXT_DIM;
                                paint.text_size = GAUGE_UNIT_FONT_SIZE;
                            });
            break;
        }
        default:
            break;
    }

    if (interaction_info != nullptr) {
        PresentationNode node = make_presentation_node(next_element_id());
        ui::InternedId region_id = next_element_id();
        node.hit_regions.push_back(HitRegion{region_id, HitShapeKind::Rectangle});

        InteractionBinding binding;
        binding.region_id = region_id;
        binding.action_id = region_id;
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
        node.interactions.push_back(std::move(binding));
        append_placement(result.layout, node.element_id,
                         bounds.x + interaction_info->bounds_x,
                         bounds.y + interaction_info->bounds_y,
                         interaction_info->bounds_w,
                         interaction_info->bounds_h);
        result.presentation.content.root.children.push_back(std::move(node));
    }

    return result;
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

    // Auto-size: minimumNodeSize gives a compact layout driven by port indents
    // and content area, not header/footer text. Preferred size is the comfortable
    // size that shows all text. For auto-sizing fresh nodes we use minimum;
    // explicit saved dimensions are clamped to minimum as a floor.
    Pt min_sz = minimumNodeSize();

    float w = min_sz.x;
    float h = min_sz.y;

    bool has_explicit = data.layout.width.has_value() && data.layout.height.has_value();
    if (has_explicit) {
        w = std::max(*data.layout.width, min_sz.x);
        h = std::max(*data.layout.height, min_sz.y);
    }
    spdlog::debug("[widget] NodeWidget layout: node='{}' type='{}' min=({},{}) explicit_size={} final=({},{})",
                  data.view.name, type_name_, min_sz.x, min_sz.y,
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
    auto policy = content_layout_policy(content_type);
    content_preferred_size_ = policy.preferred_size;
    content_align_x_ = policy.align_x;
    content_align_y_ = policy.align_y;
    content_reserve_width_ = policy.reserve_width;
    content_reserve_height_ = policy.reserve_height;
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
        if (content_type != bp2::NodeContentType::None) {
            auto* container = layout_->emplaceChild<Container>(standard_content_margins());
            container->setFlexGrow(1.0f);
            configure_content_geometry(content_type);
            container->emplaceChild<ReservedSpace>(content_preferred_size_,
                                                   content_reserve_width_,
                                                   content_reserve_height_);
            content_container_ = container;
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
    auto* toggle_container = center_col->emplaceChild<Container>(standard_content_margins());
    configure_content_geometry(bp2::NodeContentType::VerticalToggle);
    toggle_container->emplaceChild<ReservedSpace>(content_preferred_size_,
                                                  content_reserve_width_,
                                                  content_reserve_height_);
    content_container_ = toggle_container;

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
    auto* center = body_row->emplaceChild<Container>(standard_content_margins());
    center->setFlexGrow(1.0f);

    if (content_type != bp2::NodeContentType::None) {
        configure_content_geometry(content_type);
        center->emplaceChild<ReservedSpace>(content_preferred_size_,
                                            content_reserve_width_,
                                            content_reserve_height_);
        content_container_ = center;
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

    const auto interaction_info = derive_content_interaction(
        cached_content_type_, content_preferred_size_, cached_content_max_);
    const auto presentation = build_content_presentation(
        node_iid_,
        cb,
        cached_content_type_,
        cached_content_min_,
        cached_content_max_,
        cached_content_value_,
        cached_content_label_,
        cached_content_state_,
        cached_content_tripped_,
        cached_content_unit_,
        interaction_info ? &*interaction_info : nullptr);

    content_semantic_snapshot_ = editor::presentation::build_semantic_scene_snapshot(
        presentation.presentation, presentation.layout);
    render_content_from_semantic_snapshot_ = presentation.renders_content;
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
        if (content_reserve_width_ && content_preferred_size_.x > 0) {
            ps.x = std::max(ps.x, content_preferred_size_.x + CONTENT_MARGIN_X * 2.0f);
        }
        if (content_reserve_height_ && content_preferred_size_.y > 0) {
            ps.y += content_preferred_size_.y + CONTENT_MARGIN_Y * 2.0f;
        }
    }
    
    return ps;
}

Pt NodeWidget::minimumNodeSize() const {
    Pt min_size = layout_ ? layout_->minimumSize(nullptr) : Pt(0.0f, 0.0f);
    min_size.x = std::max(min_size.x, editor_constants::PORT_LAYOUT_GRID);
    min_size.y = std::max(min_size.y, editor_constants::PORT_LAYOUT_GRID);
    return editor_math::snap_size_to_layout_grid(min_size);
}

Bounds NodeWidget::compute_content_bounds_from_layout() const {
    if ((content_preferred_size_.x <= 0.0f && content_preferred_size_.y <= 0.0f) ||
        content_container_ == nullptr) {
        return {};
    }

    // Layout owns placement, semantic content owns intrinsic size. The content
    // container reserves where content should live inside the node, while the
    // semantic content snapshot uses the intrinsic affordance size for render
    // and hit geometry. This keeps ports/footer/header layout stable without
    // shrinking interactive content when flex containers get tight.
    float cx = 0.0f;
    float cy = 0.0f;
    const Widget* walker = content_container_;
    while (walker != nullptr && walker != this) {
        cx += walker->localPos().x;
        cy += walker->localPos().y;
        walker = walker->parent();
    }

    const float cw = content_container_->size().x;
    const float ch = content_container_->size().y;
    const float bw = content_preferred_size_.x;
    const float bh = content_preferred_size_.y;
    const float bx = cx + (cw - bw) * content_align_x_;
    const float by = cy + (ch - bh) * content_align_y_;
    return Bounds{bx, by, bw, bh};
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

    content_bounds_ = compute_content_bounds_from_layout();

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
                const LinePaintGeometry line = resolve_line_paint_geometry(object, pos, ctx);
                dl->add_line(line.a, line.b, object.fill_color, object.stroke_width * ctx.zoom);
                continue;
            }
            if (object.primitive == editor::presentation::PaintPrimitiveKind::Arc) {
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
            Pt min = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
            Pt max = ctx.world_to_screen(Pt(pos.x + object.bounds.x + object.bounds.w,
                                            pos.y + object.bounds.y + object.bounds.h));
            dl->add_rect(min, max, DEBUG_PAINT_BOUNDS_COLOR, 1.0f);
            continue;
        }

        if (object.primitive == editor::presentation::PaintPrimitiveKind::Circle) {
            Pt center = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
            float radius = object.bounds.w * ctx.zoom;
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
            Pt center = ctx.world_to_screen(Pt(pos.x + object.bounds.x, pos.y + object.bounds.y));
            float radius = object.bounds.w * ctx.zoom;
            dl->add_rect(Pt(center.x - radius, center.y - radius),
                         Pt(center.x + radius, center.y + radius),
                         DEBUG_PAINT_BOUNDS_COLOR,
                         1.0f);
        }
    }
}

} // namespace visual
