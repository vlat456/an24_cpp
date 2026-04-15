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
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

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
                             : fallback_text_width(text, PortConstants::LABEL_FONT_SIZE);
    metrics.label_height = PortConstants::LABEL_FONT_SIZE;
    return metrics;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

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
    build(data, render_iface, interner);

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
                       const ui::StringInterner& interner) {
    // Header
    header_ = emplaceChild<HeaderStrip>(name_);

    // Footer
    footer_ = emplaceChild<FooterTypeLabel>(type_name_);

    // Content geometry
    cached_content_type_ = data.view.content_type;
    cached_content_min_ = data.view.content_min;
    cached_content_max_ = data.view.content_max;
    cached_content_value_ = data.view.content_value;
    cached_content_label_ = data.view.content_label;
    cached_content_state_ = data.view.content_state;
    cached_content_tripped_ = data.view.content_tripped;
    cached_content_unit_ = data.view.content_unit;

    if (cached_content_type_ != bp2::NodeContentType::None) {
        configure_content_geometry(cached_content_type_);
    }

    // Resolve port layout — always use the universal resolver.
    const std::vector<bp2::NodePort> input_ports = bp2::derive_input_ports(render_iface);
    const std::vector<bp2::NodePort> output_ports = bp2::derive_output_ports(render_iface);
    auto overrides = resolve_bp2_layout_overrides(data.layout.layout_overrides);
    resolved_layout_ = resolve_port_layout(input_ports, output_ports, overrides, interner);

    // Create port + label children for each side.
    auto create_entries = [&](const std::vector<ResolvedPort>& resolved, bp2::PortLayoutSide side) {
        for (const auto& rp : resolved) {
            PortEntry entry;
            entry.layout_side = side;
            entry.logical_side = rp.logical_side;

            // Create port widget
            entry.port = emplaceChild<Port>(rp.port_name, rp.logical_side, rp.type, side);
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
        header_ ? header_->preferredSize(dl).x : 0.0f,
        footer_ ? footer_->preferredSize(dl).x : 0.0f,
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
