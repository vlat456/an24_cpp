#include "editor/visual/presentation/node_presentation.h"

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
// Registry
// ============================================================================

void NodePresenterRegistry::register_presenter(ui::InternedId type_id, NodePresenter presenter) {
    assert(presenter.content != nullptr);
    presenters_[type_id] = std::move(presenter);
}

const NodePresenter* NodePresenterRegistry::find_presenter(ui::InternedId type_id) const {
    const auto it = presenters_.find(type_id);
    return it == presenters_.end() ? nullptr : &it->second;
}

// ============================================================================
// Compile — registry-based (per-type presenter)
// ============================================================================

NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const bp2::Blueprint::Node& node,
                                           ui::InternedId type_id) {
    assert(ctx.registry != nullptr);
    const NodePresenter* presenter = ctx.registry->find_presenter(type_id);
    assert(presenter != nullptr);
    assert(presenter->content != nullptr);

    NodePresentation presentation;
    presentation.node_id = node.semantic.id;
    presentation.shell.frame_kind = presenter->frame_kind;
    presentation.shell.title = node.view.name;
    presentation.content = presenter->content(node, type_id);

    return presentation;
}

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
constexpr float GAUGE_RADIUS = 40.0f;
constexpr float GAUGE_NEEDLE_LENGTH = 32.0f;
constexpr float GAUGE_START_ANGLE = 210.0f;
constexpr float GAUGE_SWEEP_ANGLE = -240.0f;
constexpr float GAUGE_VALUE_FONT_SIZE = 14.0f;
constexpr float GAUGE_UNIT_FONT_SIZE = 10.0f;

// Colors
constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;
constexpr uint32_t COLOR_NEEDLE = 0xFF2A70C8;
constexpr uint32_t COLOR_TICK_MAJOR = 0xFFDCD5D4;
constexpr uint32_t COLOR_TICK_MINOR = 0xFF606070;
constexpr uint32_t COLOR_GAUGE_TEXT = 0xFFDCD5D4;
constexpr uint32_t COLOR_TRIPPED = 0xFF4040FF;
constexpr uint32_t COLOR_BUS_BORDER = 0xFF606068;
constexpr uint32_t COLOR_TEXT_DIM = 0xFF808080;

/// Monotonic element ID allocator for building presentation trees.
struct ElementIdAllocator {
    uint32_t next = 1;
    ui::InternedId alloc() { return ui::InternedId(next++); }
};

PresentationNode make_node(ElementIdAllocator& ids) {
    PresentationNode node;
    node.element_id = ids.alloc();
    return node;
}

PaintCommand make_paint(ElementIdAllocator& ids, PaintPrimitiveKind kind) {
    PaintCommand paint;
    paint.id = ids.alloc();
    paint.kind = kind;
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
    ui::InternedId region_id = ids.alloc();
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
                          const bp2::Blueprint::Node& node, bool vertical) {
    const bool state = node.view.content_state;
    const bool tripped = node.view.content_tripped;

    // Background
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = tripped ? COLOR_TRIPPED : (state ? 0xFF3A6830 : 0xFF1C1D24);
        paint.stroke_color = COLOR_BUS_BORDER;
        paint.stroke_width = 1.0f;
    });

    // Handle
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = tripped ? COLOR_TRIPPED : (state ? 0xFF3A6830 : 0xFF2C3038);
        paint.stroke_color = 0xFF1C1D24;
        paint.stroke_width = 1.0f;
    });

    // Click interaction
    append_interaction(root, ids, InteractionKind::Click);
}

void build_slider_content(PresentationNode& root, ElementIdAllocator& ids,
                          const bp2::Blueprint::Node& node) {
    const float min_value = node.view.content_min;
    const float max_value = node.view.content_max;
    const float value = node.view.content_value;
    const float range = max_value - min_value;
    const float t = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;

    // Track background
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF1C1D24;
    });

    // Track fill
    append_painted(root, ids, PaintPrimitiveKind::Rectangle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF3A6830;
    });

    // Handle
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF5078C0;
        paint.stroke_color = 0xFF3050A0;
        paint.stroke_width = 1.0f;
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
                             const bp2::Blueprint::Node& node) {
    const float value = node.view.content_value;
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
    });
}

void build_knob_content(PresentationNode& root, ElementIdAllocator& ids,
                        const bp2::Blueprint::Node& node) {
    const int num_positions = std::max(2, static_cast<int>(node.view.content_max));
    const int position = std::clamp(static_cast<int>(node.view.content_value), 0, num_positions - 1);

    // Knob body
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF3A3A42;
        paint.stroke_color = 0xFF606068;
        paint.stroke_width = 1.0f;
    });

    // Tick marks
    for (int i = 0; i < num_positions; ++i) {
        const float t = (num_positions > 1) ? static_cast<float>(i) / (num_positions - 1) : 0.5f;
        const float angle = KNOB_ARC_START_DEG + t * KNOB_ARC_SWEEP_DEG;
        append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
            paint.fill_color = (i == position) ? 0xFF5078C0 : 0xFF808090;
            paint.inset = KNOB_TICK_INNER;
            paint.stroke_width = (i == position) ? 2.5f : 1.5f;
            // Encode angle/radius in bounds via text_size (convention from visual_node)
            paint.text_size = angle;
        });
    }

    // Pointer line
    const float sel_t = (num_positions > 1) ? static_cast<float>(position) / (num_positions - 1) : 0.5f;
    const float sel_angle = KNOB_ARC_START_DEG + sel_t * KNOB_ARC_SWEEP_DEG;
    append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
        paint.fill_color = 0xFF5078C0;
        paint.stroke_width = 2.0f;
    });

    // Discrete drag interaction
    append_interaction(root, ids, InteractionKind::DragDiscrete,
                       0.0f, 100.0f, static_cast<float>(std::max(2, num_positions)));
}

void build_gauge_content(PresentationNode& root, ElementIdAllocator& ids,
                         const bp2::Blueprint::Node& node) {
    const float min_value = node.view.content_min;
    const float max_value = node.view.content_max;
    const float value = node.view.content_value;
    const float range = max_value - min_value;
    const float normalized = (range > 1e-6f) ? std::clamp((value - min_value) / range, 0.0f, 1.0f) : 0.0f;

    // Arc
    append_painted(root, ids, PaintPrimitiveKind::Arc, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_GAUGE_BORDER;
        paint.inset = GAUGE_START_ANGLE;
        paint.stroke_width = 2.0f;
    });

    // Tick marks
    for (int i = 0; i < 11; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const bool is_major = (i % 5) == 0;
        append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
            paint.fill_color = is_major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR;
            paint.inset = GAUGE_RADIUS - (is_major ? 6.0f : 3.0f);
            paint.stroke_width = 1.5f;
        });
    }

    // Needle
    const float needle_angle = GAUGE_START_ANGLE + normalized * GAUGE_SWEEP_ANGLE;
    append_painted(root, ids, PaintPrimitiveKind::Line, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_NEEDLE;
        paint.stroke_width = 2.0f;
    });

    // Center dot
    append_painted(root, ids, PaintPrimitiveKind::Circle, [&](PaintCommand& paint) {
        paint.fill_color = COLOR_NEEDLE;
    });

    // Value text
    append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", value);
        paint.text = buf;
        paint.fill_color = COLOR_GAUGE_TEXT;
        paint.text_size = GAUGE_VALUE_FONT_SIZE;
    });

    // Unit text
    if (!node.view.content_unit.empty()) {
        append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
            paint.text = node.view.content_unit;
            paint.fill_color = COLOR_TEXT_DIM;
            paint.text_size = GAUGE_UNIT_FONT_SIZE;
        });
    }
}

void build_text_content(PresentationNode& root, ElementIdAllocator& ids,
                        const bp2::Blueprint::Node& node) {
    if (!node.view.content_label.empty()) {
        append_painted(root, ids, PaintPrimitiveKind::Text, [&](PaintCommand& paint) {
            paint.text = node.view.content_label;
        });
    }
}

} // namespace

PresentationNode default_content_presenter(const bp2::Blueprint::Node& node, ui::InternedId /*type_id*/) {
    ElementIdAllocator ids;
    PresentationNode root = make_node(ids);
    root.layout = LayoutKind::Overlay;

    switch (node.view.content_type) {
        case bp2::NodeContentType::Switch:
            build_switch_content(root, ids, node, false);
            break;
        case bp2::NodeContentType::VerticalToggle:
            build_switch_content(root, ids, node, true);
            break;
        case bp2::NodeContentType::Slider:
            build_slider_content(root, ids, node);
            break;
        case bp2::NodeContentType::Indicator:
            build_indicator_content(root, ids, node);
            break;
        case bp2::NodeContentType::Knob:
            build_knob_content(root, ids, node);
            break;
        case bp2::NodeContentType::Gauge:
            build_gauge_content(root, ids, node);
            break;
        case bp2::NodeContentType::Text:
            build_text_content(root, ids, node);
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
// Compile — render_hint-based (no registry needed)
// ============================================================================

NodePresentation compile_node_presentation(const bp2::Blueprint::Node& node) {
    NodePresentation presentation;
    presentation.node_id = node.semantic.id;
    presentation.shell.frame_kind = classify_frame_kind(node.view.render_hint);
    presentation.shell.title = node.view.name;

    // Populate shell metadata based on frame kind
    switch (presentation.shell.frame_kind) {
        case NodeFrameKind::Annotation:
            // Text nodes: store body text in annotation_text
            {
                auto it = node.semantic.string_params.find("text");
                if (it != node.semantic.string_params.end()) {
                    presentation.shell.annotation_text = it->second;
                }
                auto font_it = node.semantic.string_params.find("font_size");
                if (font_it != node.semantic.string_params.end()) {
                    char* end = nullptr;
                    float parsed = std::strtof(font_it->second.c_str(), &end);
                    if (end != font_it->second.c_str() && parsed > 0.0f) {
                        presentation.shell.annotation_font_size = parsed;
                    }
                }
            }
            break;
        case NodeFrameKind::Reference:
        case NodeFrameKind::Bus:
        case NodeFrameKind::Group:
            break;
        case NodeFrameKind::Standard:
            // Standard nodes get a type_name footer
            // (type_id is in semantic.type, but we don't have the interner here;
            //  the raw InternedId is sufficient for downstream lookup)
            break;
    }

    // Compile content tree
    presentation.content = default_content_presenter(node, node.semantic.type);

    return presentation;
}

} // namespace editor::presentation
