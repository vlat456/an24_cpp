#include "editor/visual/presentation/node_presentation.h"
#include "data/node_content.h"
#include "core/model/component_spec.h"
#include "core/model/presentation_spec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace editor::presentation {

// ============================================================================
// Frame kind classification
// ============================================================================

NodeFrameKind classify_frame_kind(std::string_view render_hint) {
    if (render_hint == "ref")   return NodeFrameKind::Reference;
    if (render_hint == "bus")   return NodeFrameKind::Bus;
    if (render_hint == "group") return NodeFrameKind::Group;
    if (render_hint == "text")  return NodeFrameKind::Annotation;
    return NodeFrameKind::Standard;
}

// ============================================================================
// Canonical frame kind resolution from ComponentSpec
// ============================================================================

NodeFrameKind resolve_frame_kind(const ComponentSpec* def, const TypePresentation* pres) {
    if (!def) return NodeFrameKind::Standard;
    const std::string& hint = pres ? pres->render_hint : "";
    if (hint.empty()) return NodeFrameKind::Standard;
    return classify_frame_kind(hint);
}

// ============================================================================
// Canonical CompiledPresentationSpec from ComponentSpec + TypePresentation + semantic params
// ============================================================================

CompiledPresentationSpec make_presentation_spec(const bp2::Blueprint::Node& node,
                                         const ComponentSpec* def,
                                         const TypePresentation* pres,
                                         core::StringInterner& interner,
                                         const editor::RuntimeNodeState* runtime_state) {
    CompiledPresentationSpec spec;
    spec.node_id = node.semantic.id;
    spec.type_id = node.semantic.type;
    spec.frame_kind = resolve_frame_kind(def, pres);
    spec.title = node.view.name;  // name is tier-1 canonical, OK to read from view

    // Full runtime content: canonical static semantics plus current dynamic state.
    NodeContent nc = def ? create_runtime_node_content(node, *def, pres, interner, runtime_state) : NodeContent{};
    spec.content_type = nc.type;
    spec.content_label = nc.label;
    spec.content_min = nc.min;
    spec.content_max = nc.max;
    spec.content_unit = nc.unit;
    spec.content_value = nc.value;
    spec.content_state = nc.state;

    // Annotation params from semantic string_params
    if (spec.frame_kind == NodeFrameKind::Annotation) {
        auto it = node.semantic.string_params.find("text");
        if (it != node.semantic.string_params.end()) {
            spec.annotation_text = it->second;
        }
        auto font_it = node.semantic.string_params.find("font_size");
        if (font_it != node.semantic.string_params.end()) {
            char* end = nullptr;
            float parsed = std::strtof(font_it->second.c_str(), &end);
            if (end != font_it->second.c_str() && parsed > 0.0f) {
                spec.annotation_font_size = parsed;
            }
        }
    }

    // Static badges — resolved at scene-build time from structural properties
    if (node.is_blueprint_instance()) {
        spec.static_badges.set(NodeBadge::Composite);
    }

    return spec;
}

// ============================================================================
// Registry
// ============================================================================

void NodePresenterRegistry::register_presenter(core::InternedId type_id, NodePresenter presenter) {
    assert(presenter.content != nullptr);
    presenters_[type_id] = std::move(presenter);
}

const NodePresenter* NodePresenterRegistry::find_presenter(core::InternedId type_id) const {
    const auto it = presenters_.find(type_id);
    return it == presenters_.end() ? nullptr : &it->second;
}

namespace {

void populate_shell_metadata(NodeShellModel& shell,
                             const NodePresentationCompileContext& ctx,
                             const PresentationSpec& spec) {
    if (shell.frame_kind == NodeFrameKind::Standard && ctx.resolve_type_name != nullptr) {
        shell.type_name = std::string(ctx.resolve_type_name(spec.type_id, ctx.resolve_type_name_user_data));
    }

    if (shell.frame_kind != NodeFrameKind::Annotation) {
        return;
    }

    shell.annotation_text = spec.annotation_text;
    shell.annotation_font_size = spec.annotation_font_size;
}

} // namespace

// ============================================================================
// Default content presenter — handles all NodeContentType variants
// ============================================================================

namespace {

// Content rendering constants (extracted from visual_node.cpp)
constexpr float SWITCH_WIDTH = 48.0f;
constexpr float SWITCH_HEIGHT = 20.0f;
constexpr float VERTICAL_TOGGLE_WIDTH = 16.0f;
constexpr float VERTICAL_TOGGLE_HEIGHT = 48.0f;
constexpr float SLIDER_HEIGHT = 16.0f;
constexpr float SLIDER_WIDTH = 60.0f;
constexpr float SLIDER_TRACK_HEIGHT = 4.0f;
constexpr float SLIDER_HANDLE_RADIUS = 6.0f;
constexpr float INDICATOR_SIZE = 24.0f;
constexpr float KNOB_SIZE = 48.0f;
constexpr float KNOB_RADIUS = 16.0f;
constexpr float KNOB_TICK_INNER = 20.0f;
constexpr float KNOB_TICK_OUTER = 24.0f;
constexpr float KNOB_ARC_START_DEG = 225.0f;
constexpr float KNOB_ARC_SWEEP_DEG = -270.0f;
// Colors
constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;
constexpr uint32_t COLOR_NEEDLE = 0xFF2A70C8;
constexpr uint32_t COLOR_TICK_MAJOR = 0xFFDCD5D4;
constexpr uint32_t COLOR_TICK_MINOR = 0xFF606070;
constexpr uint32_t COLOR_GAUGE_TEXT = 0xFFDCD5D4;
constexpr uint32_t COLOR_BUS_BORDER = 0xFF606068;
constexpr uint32_t COLOR_TEXT_DIM = 0xFF808080;

/// Monotonic element ID allocator for building presentation trees.
struct ElementIdAllocator {
    uint32_t next = 1;
    core::InternedId alloc() { return core::InternedId(next++); }
};

PresentationNode make_node(ElementIdAllocator& ids) {
    PresentationNode node;
    node.element_id = ids.alloc();
    return node;
}

PrimitiveGeometry default_geometry_for(PaintPrimitiveKind kind) {
    switch (kind) {
        case PaintPrimitiveKind::Rectangle: return RectGeometry{};
        case PaintPrimitiveKind::Circle:    return CircleGeometry{};
        case PaintPrimitiveKind::Line:      return LineGeometry{};
        case PaintPrimitiveKind::Arc:       return ArcGeometry{};
        case PaintPrimitiveKind::Text:
        default:                            return TextGeometry{};
    }
}

PaintCommand make_paint(ElementIdAllocator& ids, PaintPrimitiveKind kind) {
    PaintCommand paint;
    paint.id = ids.alloc();
    paint.kind = kind;
    paint.geometry = default_geometry_for(kind);
    return paint;
}

/// Append a painted child to a parent node.
template <typename ConfigureFn>
void append_painted(PresentationNode& parent,
                    ElementIdAllocator& ids,
                    PaintPrimitiveKind kind,
                    ConfigureFn configure) {
    PresentationNode child = make_node(ids);
    PaintCommand paint = make_paint(ids, kind);
    configure(paint);
    if (kind == PaintPrimitiveKind::Rectangle) {
        const auto* rect = std::get_if<RectGeometry>(&paint.geometry);
        assert(rect != nullptr);
        assert(rect->w > 0.0f);
        assert(rect->h > 0.0f);
    }
    child.paint.push_back(std::move(paint));
    parent.children.push_back(std::move(child));
}

/// Append an interactive hit region child to a parent node.
void append_interaction(PresentationNode& parent,
                        ElementIdAllocator& ids,
                        InteractionKind interaction_kind,
                        float min_value = 0.0f,
                        float max_value = 0.0f,
                        float step = 0.0f) {
    PresentationNode child = make_node(ids);
    core::InternedId region_id = ids.alloc();
    child.hit_regions.push_back(HitRegion{region_id, HitShapeKind::Rectangle});

    InteractionBinding binding;
    binding.region_id = region_id;
    binding.kind = interaction_kind;
    binding.action_id = region_id;
    binding.min_value = min_value;
    binding.max_value = max_value;
    binding.step = step;
    child.interactions.push_back(std::move(binding));
    parent.children.push_back(std::move(child));
}

void build_switch_content(PresentationNode& root, ElementIdAllocator& ids,
                          const PresentationSpec& spec, bool vertical) {
    const bool state = spec.content_state;

    const float bg_w = vertical ? VERTICAL_TOGGLE_WIDTH : SWITCH_WIDTH;
    const float bg_h = vertical ? VERTICAL_TOGGLE_HEIGHT : SWITCH_HEIGHT;

    // Background — fills entire element bounds
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = state ? 0xFF3A6830 : 0xFF1C1D24;
        paint.stroke_color = COLOR_BUS_BORDER;
        paint.stroke_width = 1.0f;
        paint.geometry = RectGeometry{0.0f, 0.0f, bg_w, bg_h};
    });

    // Handle — positioned based on state
    const RectGeometry handle_geo = vertical
        ? RectGeometry{0.0f, state ? (bg_h * 0.15f) : (bg_h * 0.70f), bg_w, bg_h * 0.24f}
        : RectGeometry{state ? (bg_w - bg_w * 0.40f) : 0.0f, 0.0f, bg_w * 0.40f, bg_h};
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = state ? 0xFF3A6830 : 0xFF2C3038;
        paint.stroke_color = 0xFF1C1D24;
        paint.stroke_width = 1.0f;
        paint.geometry = handle_geo;
    });

    // Click interaction
    append_interaction(root, ids, InteractionKind::Click);
}

void build_slider_content(PresentationNode& root, ElementIdAllocator& ids,
                          const PresentationSpec& spec) {
    const float min_value = spec.content_min;
    const float max_value = spec.content_max;
    const float value = spec.content_value;
    const float range = max_value - min_value;
    const float t = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;

    // Track background
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF1C1D24;
        paint.geometry = RectGeometry{SLIDER_HANDLE_RADIUS,
                                      (SLIDER_HEIGHT - SLIDER_TRACK_HEIGHT) * 0.5f,
                                      SLIDER_WIDTH - 2.0f * SLIDER_HANDLE_RADIUS,
                                      SLIDER_TRACK_HEIGHT};
    });

    // Track fill — only emitted when the slider has a nonzero fill fraction.
    // At t == 0 the fill rectangle would have zero width, which violates the
    // positive-geometry invariant enforced by append_painted().
    if (t > 0.0f) {
        append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
            paint.fill_color = 0xFF3A6830;
            paint.geometry = RectGeometry{SLIDER_HANDLE_RADIUS,
                                          (SLIDER_HEIGHT - SLIDER_TRACK_HEIGHT) * 0.5f,
                                          t * (SLIDER_WIDTH - 2.0f * SLIDER_HANDLE_RADIUS),
                                          SLIDER_TRACK_HEIGHT};
        });
    }

    // Handle
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF5078C0;
        paint.stroke_color = 0xFF3050A0;
        paint.stroke_width = 1.0f;
        paint.geometry = CircleGeometry{(t - 0.5f) * (SLIDER_WIDTH - 2.0f * SLIDER_HANDLE_RADIUS),
                                        0.0f,
                                        SLIDER_HANDLE_RADIUS};
    });

    // Value label
    append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", value);
        paint.text = buf;
    });

    // Drag interaction
    const float pad = SLIDER_HANDLE_RADIUS;
    const float track_w = SLIDER_WIDTH - 2.0f * pad;
    append_interaction(root, ids, InteractionKind::DragScalar, pad, pad + track_w);
}

void build_indicator_content(PresentationNode& root, ElementIdAllocator& ids,
                             const PresentationSpec& spec) {
    const float value = spec.content_value;
    const float b = std::clamp(value, 0.0f, 1.0f);

    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
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
        paint.geometry = CircleGeometry{0.0f, 0.0f, INDICATOR_SIZE * (0.3f + 0.15f * b)};
    });
}

void build_knob_content(PresentationNode& root, ElementIdAllocator& ids,
                        const PresentationSpec& spec) {
    const int num_positions = std::max(2, static_cast<int>(spec.content_max));
    const int position = std::clamp(static_cast<int>(spec.content_value), 0, num_positions - 1);

    // Knob body
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF3A3A42;
        paint.stroke_color = 0xFF606068;
        paint.stroke_width = 1.0f;
        paint.geometry = CircleGeometry{0.0f, 0.0f, KNOB_RADIUS};
    });

    // Tick marks
    for (int i = 0; i < num_positions; ++i) {
        const float t = (num_positions > 1) ? static_cast<float>(i) / (num_positions - 1) : 0.5f;
        const float angle = KNOB_ARC_START_DEG + t * KNOB_ARC_SWEEP_DEG;
        append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
            paint.fill_color = (i == position) ? 0xFF5078C0 : 0xFF808090;
            paint.stroke_width = (i == position) ? 2.5f : 1.5f;
            paint.geometry = LineGeometry{0.0f, 0.0f, angle, KNOB_TICK_INNER, KNOB_TICK_OUTER};
        });
    }

    // Pointer line
    const float sel_t = (num_positions > 1) ? static_cast<float>(position) / (num_positions - 1) : 0.5f;
    const float sel_angle = KNOB_ARC_START_DEG + sel_t * KNOB_ARC_SWEEP_DEG;
    append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF5078C0;
        paint.stroke_width = 2.0f;
        paint.geometry = LineGeometry{0.0f, 0.0f, sel_angle, 0.0f, KNOB_RADIUS * 0.85f};
    });

    // Discrete drag interaction
    append_interaction(root, ids, InteractionKind::DragDiscrete,
                       0.0f, 100.0f, static_cast<float>(std::max(2, num_positions)));
}

void build_gauge_content(PresentationNode& root, ElementIdAllocator& ids,
                         const PresentationSpec& spec) {
    constexpr GaugeMetrics metrics = gauge_metrics();
    const float min_value = spec.content_min;
    const float max_value = spec.content_max;
    const float value = spec.content_value;
    const float range = max_value - min_value;
    const float normalized = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;

    // Arc
    append_painted(root, ids, PaintPrimitiveKind::Arc, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_GAUGE_BORDER;
        paint.stroke_width = 2.0f;
        paint.geometry = ArcGeometry{0.0f, metrics.center_offset_y(), metrics.radius,
                                     metrics.start_angle_deg, metrics.sweep_angle_deg};
    });

    // Tick marks
    for (int i = 0; i < 11; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const float angle = metrics.start_angle_deg + t * metrics.sweep_angle_deg;
        const bool is_major = (i % 5) == 0;
        append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
            paint.fill_color = is_major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR;
            paint.stroke_width = 1.5f;
            paint.geometry = LineGeometry{0.0f, metrics.center_offset_y(), angle,
                                          is_major ? metrics.major_tick_inner_radius() : metrics.minor_tick_inner_radius(),
                                          metrics.radius};
        });
    }

    // Needle
    const float needle_angle = metrics.start_angle_deg + normalized * metrics.sweep_angle_deg;
    append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_NEEDLE;
        paint.stroke_width = 2.0f;
        paint.geometry = LineGeometry{0.0f, metrics.center_offset_y(), needle_angle, 0.0f, metrics.needle_length};
    });

    // Center dot
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_NEEDLE;
        paint.geometry = CircleGeometry{0.0f, metrics.center_offset_y(), metrics.center_dot_radius};
    });

    // Value text
    append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", value);
        paint.text = buf;
        paint.fill_color = COLOR_GAUGE_TEXT;
        paint.geometry = TextGeometry{0.0f, metrics.value_text_y(), metrics.value_font_size, true};
    });

    // Unit text
    if (!spec.content_unit.empty()) {
        append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
            paint.text = spec.content_unit;
            paint.fill_color = COLOR_TEXT_DIM;
            paint.geometry = TextGeometry{0.0f, metrics.unit_text_y(), metrics.unit_font_size, true};
        });
    }
}

void build_text_content(PresentationNode& root, ElementIdAllocator& ids,
                        const PresentationSpec& spec) {
    if (!spec.content_label.empty()) {
        append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
            paint.text = spec.content_label;
        });
    }
}

} // namespace

PresentationNode default_content_presenter(const PresentationSpec& spec) {
    ElementIdAllocator ids;
    PresentationNode root = make_node(ids);
    root.layout = LayoutKind::Overlay;

    switch (spec.content_type) {
        case bp2::NodeContentType::Switch:
            build_switch_content(root, ids, spec, false);
            break;
        case bp2::NodeContentType::VerticalToggle:
            build_switch_content(root, ids, spec, true);
            break;
        case bp2::NodeContentType::Slider:
            build_slider_content(root, ids, spec);
            break;
        case bp2::NodeContentType::Indicator:
            build_indicator_content(root, ids, spec);
            break;
        case bp2::NodeContentType::Knob:
            build_knob_content(root, ids, spec);
            break;
        case bp2::NodeContentType::Gauge:
            build_gauge_content(root, ids, spec);
            break;
        case bp2::NodeContentType::Text:
            build_text_content(root, ids, spec);
            break;
        case bp2::NodeContentType::None:
        case bp2::NodeContentType::Value:
        case bp2::NodeContentType::Count:
        default:
            break;
    }

    return root;
}

// ============================================================================
// Compile — unified entry point
// ============================================================================

NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const PresentationSpec& spec) {
    NodePresentation presentation;
    presentation.node_id = spec.node_id;
    presentation.shell.title = spec.title;

    // Determine frame_kind: registry presenter wins over spec if present
    if (ctx.registry != nullptr) {
        const NodePresenter* presenter = ctx.registry->find_presenter(spec.type_id);
        if (presenter != nullptr) {
            assert(presenter->content != nullptr);
            presentation.shell.frame_kind = presenter->frame_kind;
            populate_shell_metadata(presentation.shell, ctx, spec);
            presentation.content = presenter->content(spec);
            return presentation;
        }
    }

    // Fall back to default content presenter
    presentation.shell.frame_kind = spec.frame_kind;
    populate_shell_metadata(presentation.shell, ctx, spec);
    presentation.content = default_content_presenter(spec);
    return presentation;
}

NodePresentation compile_node_presentation(const PresentationSpec& spec) {
    return compile_node_presentation(NodePresentationCompileContext{}, spec);
}

} // namespace editor::presentation
