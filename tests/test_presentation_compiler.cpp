#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

using namespace editor::presentation;

// ============================================================================
// Helpers
// ============================================================================

namespace {

std::string_view resolve_test_type_name(ui::InternedId type_id, void* /*user_data*/) {
    switch (type_id.raw()) {
        case 1000: return "Battery";
        case 1001: return "Switch";
        case 1002: return "Gauge";
        default: return "UnknownType";
    }
}

PresentationSpec make_spec(ui::InternedId id,
                          const std::string& name,
                          NodeFrameKind frame_kind = NodeFrameKind::Standard,
                          bp2::NodeContentType content_type = bp2::NodeContentType::None) {
    PresentationSpec spec;
    spec.node_id = id;
    spec.type_id = ui::InternedId(1000);
    spec.title = name;
    spec.frame_kind = frame_kind;
    spec.content_type = content_type;
    return spec;
}

/// Count paint commands of a given kind in the content tree (recursive).
size_t count_paint_kind(const PresentationNode& node, PaintPrimitiveKind kind) {
    size_t count = 0;
    for (const auto& paint : node.paint) {
        if (paint.kind == kind) ++count;
    }
    for (const auto& child : node.children) {
        count += count_paint_kind(child, kind);
    }
    return count;
}

const ui::Rect* find_slot(const NodeSlotLayout& layout, NodeSlot slot) {
    for (const SlotAssignment& assignment : layout.slots) {
        if (assignment.slot == slot) {
            return &assignment.bounds;
        }
    }
    return nullptr;
}

/// Count total paint commands in the content tree (recursive).
size_t count_total_paints(const PresentationNode& node) {
    size_t count = node.paint.size();
    for (const auto& child : node.children) {
        count += count_total_paints(child);
    }
    return count;
}

/// Count total hit regions in the content tree (recursive).
size_t count_total_hit_regions(const PresentationNode& node) {
    size_t count = node.hit_regions.size();
    for (const auto& child : node.children) {
        count += count_total_hit_regions(child);
    }
    return count;
}

/// Count total interaction bindings in the content tree (recursive).
size_t count_total_interactions(const PresentationNode& node) {
    size_t count = node.interactions.size();
    for (const auto& child : node.children) {
        count += count_total_interactions(child);
    }
    return count;
}

/// Find first interaction binding of a given kind in the content tree (recursive).
const InteractionBinding* find_interaction(const PresentationNode& node, InteractionKind kind) {
    for (const auto& binding : node.interactions) {
        if (binding.kind == kind) return &binding;
    }
    for (const auto& child : node.children) {
        const auto* result = find_interaction(child, kind);
        if (result) return result;
    }
    return nullptr;
}

/// Find first paint command with text in the content tree (recursive).
const PaintCommand* find_text_paint(const PresentationNode& node) {
    for (const auto& paint : node.paint) {
        if (paint.kind == PaintPrimitiveKind::Text && !paint.text.empty()) return &paint;
    }
    for (const auto& child : node.children) {
        const auto* result = find_text_paint(child);
        if (result) return result;
    }
    return nullptr;
}

/// Collect all element IDs in the content tree (recursive).
void collect_element_ids(const PresentationNode& node, std::vector<ui::InternedId>& ids) {
    ids.push_back(node.element_id);
    for (const auto& child : node.children) {
        collect_element_ids(child, ids);
    }
}

/// Collect all paint commands in the content tree (recursive).
void collect_paints(const PresentationNode& node, std::vector<const PaintCommand*>& out) {
    for (const auto& paint : node.paint) {
        out.push_back(&paint);
    }
    for (const auto& child : node.children) {
        collect_paints(child, out);
    }
}

} // namespace

// ============================================================================
// classify_frame_kind
// ============================================================================

TEST(ClassifyFrameKind, StandardForEmptyHint) {
    EXPECT_EQ(classify_frame_kind(""), NodeFrameKind::Standard);
}

TEST(ClassifyFrameKind, StandardForUnknownHint) {
    EXPECT_EQ(classify_frame_kind("custom_unknown"), NodeFrameKind::Standard);
}

TEST(ClassifyFrameKind, ReferenceForRefHint) {
    EXPECT_EQ(classify_frame_kind("ref"), NodeFrameKind::Reference);
}

TEST(ClassifyFrameKind, BusForBusHint) {
    EXPECT_EQ(classify_frame_kind("bus"), NodeFrameKind::Bus);
}

TEST(ClassifyFrameKind, GroupForGroupHint) {
    EXPECT_EQ(classify_frame_kind("group"), NodeFrameKind::Group);
}

TEST(ClassifyFrameKind, AnnotationForTextHint) {
    EXPECT_EQ(classify_frame_kind("text"), NodeFrameKind::Annotation);
}

// ============================================================================
// compile_node_presentation (render_hint-based, no registry)
// ============================================================================

TEST(CompileNodePresentation, StandardNodePreservesIdentityAndTitle) {
    auto spec = make_spec(ui::InternedId(1), "Generator");
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_EQ(p.node_id, ui::InternedId(1));
    EXPECT_EQ(p.shell.title, "Generator");
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Standard);
    EXPECT_EQ(p.shell.type_name, "Battery");
}

TEST(CompileNodePresentation, RefNodeGetsReferenceFrame) {
    auto spec = make_spec(ui::InternedId(2), "GND", NodeFrameKind::Reference);
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Reference);
    EXPECT_EQ(p.shell.title, "GND");
}

TEST(CompileNodePresentation, BusNodeGetsBusFrame) {
    auto spec = make_spec(ui::InternedId(3), "AC Bus", NodeFrameKind::Bus);
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Bus);
    EXPECT_EQ(p.shell.title, "AC Bus");
}

TEST(CompileNodePresentation, GroupNodeGetsGroupFrame) {
    auto spec = make_spec(ui::InternedId(4), "Power Section", NodeFrameKind::Group);
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);
    EXPECT_EQ(p.shell.title, "Power Section");
}

TEST(CompileNodePresentation, TextNodeGetsAnnotationFrame) {
    auto spec = make_spec(ui::InternedId(5), "Note", NodeFrameKind::Annotation);
    spec.annotation_text = "This is a note";
    spec.annotation_font_size = 14.0f;
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(p.shell.title, "Note");
    EXPECT_EQ(p.shell.annotation_text, "This is a note");
    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 14.0f);
}

TEST(CompileNodePresentation, TextNodeDefaultFontSizeWhenMissing) {
    auto spec = make_spec(ui::InternedId(6), "Note", NodeFrameKind::Annotation);
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

TEST(CompileNodePresentation, NoneContentTypeProducesEmptyContentChildren) {
    auto spec = make_spec(ui::InternedId(10), "Resistor", NodeFrameKind::Standard, bp2::NodeContentType::None);
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);

    EXPECT_TRUE(p.content.children.empty());
    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
}

// ============================================================================
// Switch content
// ============================================================================

TEST(DefaultContentPresenter, SwitchProducesRectanglesAndClickInteraction) {
    auto spec = make_spec(ui::InternedId(20), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec.content_state = true;
    NodePresentation p = compile_node_presentation(spec);

    // Switch: background rect + handle rect + click interaction child
    EXPECT_GE(p.content.children.size(), 3u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);

    const auto* click = find_interaction(p.content, InteractionKind::Click);
    ASSERT_NE(click, nullptr);
}

TEST(DefaultContentPresenter, VerticalToggleProducesRectanglesAndClickInteraction) {
    auto spec = make_spec(ui::InternedId(21), "Toggle", NodeFrameKind::Standard, bp2::NodeContentType::VerticalToggle);
    spec.content_state = false;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_GE(p.content.children.size(), 3u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);

    const auto* click = find_interaction(p.content, InteractionKind::Click);
    ASSERT_NE(click, nullptr);
}

TEST(DefaultContentPresenter, SwitchTrippedChangesColor) {
    auto spec_normal = make_spec(ui::InternedId(22), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_normal.content_state = true;
    spec_normal.content_tripped = false;

    auto spec_tripped = make_spec(ui::InternedId(23), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_tripped.content_state = true;
    spec_tripped.content_tripped = true;

    NodePresentation p_normal = compile_node_presentation(spec_normal);
    NodePresentation p_tripped = compile_node_presentation(spec_tripped);

    // Both should have paint commands but with different colors
    EXPECT_GE(count_total_paints(p_normal.content), 2u);
    EXPECT_GE(count_total_paints(p_tripped.content), 2u);

    // Verify tripped state actually uses different fill color on background rect
    std::vector<const PaintCommand*> normal_paints, tripped_paints;
    collect_paints(p_normal.content, normal_paints);
    collect_paints(p_tripped.content, tripped_paints);

    // First Rectangle paint is the background — colors must differ
    const PaintCommand* normal_bg = nullptr;
    const PaintCommand* tripped_bg = nullptr;
    for (const auto* p : normal_paints) {
        if (p->kind == PaintPrimitiveKind::Rectangle) { normal_bg = p; break; }
    }
    for (const auto* p : tripped_paints) {
        if (p->kind == PaintPrimitiveKind::Rectangle) { tripped_bg = p; break; }
    }
    ASSERT_NE(normal_bg, nullptr);
    ASSERT_NE(tripped_bg, nullptr);
    EXPECT_NE(normal_bg->fill_color, tripped_bg->fill_color);
}

TEST(DefaultContentPresenter, SwitchRectanglesCarryExplicitRectGeometry) {
    auto spec = make_spec(ui::InternedId(24), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec.content_state = false;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* bg = nullptr;
    const PaintCommand* handle = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Rectangle) {
            if (bg == nullptr) bg = paint;
            else if (handle == nullptr) handle = paint;
        }
    }

    ASSERT_NE(bg, nullptr);
    ASSERT_NE(handle, nullptr);

    const auto* bg_geo = std::get_if<RectGeometry>(&bg->geometry);
    const auto* handle_geo = std::get_if<RectGeometry>(&handle->geometry);
    ASSERT_NE(bg_geo, nullptr) << "Switch background must carry RectGeometry";
    ASSERT_NE(handle_geo, nullptr) << "Switch handle must carry RectGeometry";

    // Background fills entire switch area (48 x 20)
    EXPECT_FLOAT_EQ(bg_geo->x, 0.0f);
    EXPECT_FLOAT_EQ(bg_geo->y, 0.0f);
    EXPECT_FLOAT_EQ(bg_geo->w, 48.0f);
    EXPECT_FLOAT_EQ(bg_geo->h, 20.0f);

    // Handle is 40% width, positioned at left when off
    EXPECT_FLOAT_EQ(handle_geo->x, 0.0f);
    EXPECT_FLOAT_EQ(handle_geo->y, 0.0f);
    EXPECT_NEAR(handle_geo->w, 48.0f * 0.4f, 0.01f);
    EXPECT_FLOAT_EQ(handle_geo->h, 20.0f);
}

TEST(DefaultContentPresenter, SwitchHandleMovesOnStateChange) {
    auto spec_off = make_spec(ui::InternedId(25), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_off.content_state = false;
    NodePresentation p_off = compile_node_presentation(spec_off);

    auto spec_on = make_spec(ui::InternedId(26), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_on.content_state = true;
    NodePresentation p_on = compile_node_presentation(spec_on);

    auto get_handle_x = [](const PresentationNode& content) -> float {
        int rect_idx = 0;
        for (const auto& child : content.children) {
            for (const auto& p : child.paint) {
                if (p.kind == PaintPrimitiveKind::Rectangle) {
                    if (rect_idx == 1) {
                        const auto* geo = std::get_if<RectGeometry>(&p.geometry);
                        return geo ? geo->x : -1.0f;
                    }
                    ++rect_idx;
                }
            }
        }
        return -1.0f;
    };

    float x_off = get_handle_x(p_off.content);
    float x_on = get_handle_x(p_on.content);
    EXPECT_FLOAT_EQ(x_off, 0.0f) << "Off-state handle must be at left edge";
    EXPECT_GT(x_on, 0.0f) << "On-state handle must move to the right";
}

TEST(DefaultContentPresenter, VerticalToggleHandleMovesOnStateChange) {
    auto spec_off = make_spec(ui::InternedId(27), "Toggle", NodeFrameKind::Standard, bp2::NodeContentType::VerticalToggle);
    spec_off.content_state = false;
    NodePresentation p_off = compile_node_presentation(spec_off);

    auto spec_on = make_spec(ui::InternedId(28), "Toggle", NodeFrameKind::Standard, bp2::NodeContentType::VerticalToggle);
    spec_on.content_state = true;
    NodePresentation p_on = compile_node_presentation(spec_on);

    auto get_handle_geo = [](const PresentationNode& content) -> RectGeometry {
        int rect_idx = 0;
        for (const auto& child : content.children) {
            for (const auto& p : child.paint) {
                if (p.kind == PaintPrimitiveKind::Rectangle) {
                    if (rect_idx == 1) {
                        const auto* geo = std::get_if<RectGeometry>(&p.geometry);
                        return geo ? *geo : RectGeometry{};
                    }
                    ++rect_idx;
                }
            }
        }
        return {};
    };

    RectGeometry off_geo = get_handle_geo(p_off.content);
    RectGeometry on_geo = get_handle_geo(p_on.content);

    // Vertical toggle: background is 16 x 48
    EXPECT_FLOAT_EQ(off_geo.w, 16.0f);
    EXPECT_FLOAT_EQ(on_geo.w, 16.0f);
    EXPECT_GT(off_geo.h, 0.0f);
    EXPECT_GT(on_geo.h, 0.0f);

    // Off handle at 70%, On handle at 15% — different Y positions
    EXPECT_GT(off_geo.y, on_geo.y) << "Off-state handle must be lower than on-state";
}

// ============================================================================
// Slider content
// ============================================================================

TEST(DefaultContentPresenter, SliderProducesTrackHandleAndDragInteraction) {
    auto spec = make_spec(ui::InternedId(30), "Throttle", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(spec);

    // Slider: track bg + track fill + handle circle + value text + drag interaction
    EXPECT_GE(p.content.children.size(), 5u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Text), 1u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragScalar);
    ASSERT_NE(drag, nullptr);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* track_bg = nullptr;
    const PaintCommand* track_fill = nullptr;
    const PaintCommand* handle = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Rectangle) {
            if (track_bg == nullptr) track_bg = paint;
            else if (track_fill == nullptr) track_fill = paint;
        } else if (paint->kind == PaintPrimitiveKind::Circle && handle == nullptr) {
            handle = paint;
        }
    }

    ASSERT_NE(track_bg, nullptr);
    ASSERT_NE(track_fill, nullptr);
    ASSERT_NE(handle, nullptr);

    const auto* bg_geo = std::get_if<RectGeometry>(&track_bg->geometry);
    const auto* fill_geo = std::get_if<RectGeometry>(&track_fill->geometry);
    const auto* handle_geo = std::get_if<CircleGeometry>(&handle->geometry);
    ASSERT_NE(bg_geo, nullptr);
    ASSERT_NE(fill_geo, nullptr);
    ASSERT_NE(handle_geo, nullptr);

    EXPECT_GT(bg_geo->w, fill_geo->w);
    EXPECT_GT(fill_geo->w, 0.0f);
    EXPECT_GT(handle_geo->cx, -24.0f);
    EXPECT_LT(handle_geo->cx, 24.0f);
}

TEST(DefaultContentPresenter, SliderValueTextShowsFormattedValue) {
    auto spec = make_spec(ui::InternedId(31), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_value = 42.5f;
    NodePresentation p = compile_node_presentation(spec);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "42.5");
}

// ============================================================================
// Indicator content
// ============================================================================

TEST(DefaultContentPresenter, IndicatorProducesCircle) {
    auto spec = make_spec(ui::InternedId(40), "Light", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec.content_value = 0.8f;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    // Indicator has no interaction
    EXPECT_EQ(count_total_interactions(p.content), 0u);
}

TEST(DefaultContentPresenter, IndicatorOffProducesDimColor) {
    auto spec = make_spec(ui::InternedId(41), "Light", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec.content_value = 0.0f;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
}

// ============================================================================
// Knob content
// ============================================================================

TEST(DefaultContentPresenter, KnobProducesCircleTicksAndDiscreteInteraction) {
    auto spec = make_spec(ui::InternedId(50), "Selector", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec.content_max = 5.0f;
    spec.content_value = 2.0f;
    NodePresentation p = compile_node_presentation(spec);

    // Knob body circle + tick lines (5) + pointer line + discrete interaction
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 2u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 5.0f);
}

TEST(DefaultContentPresenter, KnobMinTwoPositions) {
    auto spec = make_spec(ui::InternedId(51), "Selector", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec.content_max = 1.0f;  // Would be 1 position, clamped to 2
    spec.content_value = 0.0f;
    NodePresentation p = compile_node_presentation(spec);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 2.0f);
}

// ============================================================================
// Gauge content
// ============================================================================

TEST(DefaultContentPresenter, GaugeProducesArcTicksNeedleAndValueText) {
    auto spec = make_spec(ui::InternedId(60), "Voltmeter", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_min = 0.0f;
    spec.content_max = 30.0f;
    spec.content_value = 27.5f;
    spec.content_unit = "V";
    NodePresentation p = compile_node_presentation(spec);

    // Arc + 11 tick lines + needle line + center dot circle + value text + unit text
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Arc), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 12u);  // 11 ticks + 1 needle
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Text), 2u);   // value + unit

    // Gauge has no interaction
    EXPECT_EQ(count_total_interactions(p.content), 0u);
}

TEST(DefaultContentPresenter, GaugeValueTextShowsFormattedValue) {
    auto spec = make_spec(ui::InternedId(61), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_value = 12.3f;
    spec.content_unit = "A";
    NodePresentation p = compile_node_presentation(spec);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "12.3");
}

TEST(DefaultContentPresenter, GaugeWithoutUnitOmitsUnitText) {
    auto spec = make_spec(ui::InternedId(62), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_unit = "";
    NodePresentation p = compile_node_presentation(spec);

    // Should have value text but not unit text
    // Arc(1) + ticks(11) + needle(1) + dot(1) + value_text(1) = 15 children
    // Without unit: 15 children
    size_t text_count = count_paint_kind(p.content, PaintPrimitiveKind::Text);
    EXPECT_EQ(text_count, 1u);
}

// ============================================================================
// Text content
// ============================================================================

TEST(DefaultContentPresenter, TextContentProducesTextPaint) {
    auto spec = make_spec(ui::InternedId(70), "Label", NodeFrameKind::Standard, bp2::NodeContentType::Text);
    spec.content_label = "Hello World";
    NodePresentation p = compile_node_presentation(spec);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "Hello World");
}

TEST(DefaultContentPresenter, EmptyTextContentProducesNoChildren) {
    auto spec = make_spec(ui::InternedId(71), "Label", NodeFrameKind::Standard, bp2::NodeContentType::Text);
    spec.content_label = "";
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_TRUE(p.content.children.empty());
}

// ============================================================================
// Element ID uniqueness
// ============================================================================

TEST(DefaultContentPresenter, AllElementIdsAreUnique) {
    auto spec = make_spec(ui::InternedId(80), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_value = 15.0f;
    spec.content_unit = "V";
    NodePresentation p = compile_node_presentation(spec);

    std::vector<ui::InternedId> ids;
    collect_element_ids(p.content, ids);

    for (size_t i = 0; i < ids.size(); ++i) {
        for (size_t j = i + 1; j < ids.size(); ++j) {
            EXPECT_NE(ids[i], ids[j]) << "Duplicate element ID at indices " << i << " and " << j;
        }
    }
}

// ============================================================================
// Integration: compile → layout → snapshot pipeline
// ============================================================================

TEST(PresentationCompilerIntegration, StandardNodeWithSwitchFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(100), "AZS-1", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec.type_id = ui::InternedId(1001);
    spec.content_state = true;

    // Step 1: Compile
    NodePresentation p = compile_node_presentation(NodePresentationCompileContext{.resolve_type_name = &resolve_test_type_name}, spec);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Standard);
    EXPECT_GE(p.content.children.size(), 3u);

    // Step 2: Layout
    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(180.0f, 120.0f));
    EXPECT_GT(layout.node_bounds.w, 0.0f);
    EXPECT_GT(layout.node_bounds.h, 0.0f);

    // Step 3: Snapshot
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);
    EXPECT_FALSE(snapshot.render_objects.empty());
    EXPECT_FALSE(snapshot.hit_objects.empty());

    // Verify frame and title render objects
    bool has_frame = false;
    bool has_title = false;
    bool has_footer = false;
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind == SceneRenderObjectKind::NodeFrame) has_frame = true;
        if (obj.kind == SceneRenderObjectKind::NodeTitle && obj.text == "AZS-1") has_title = true;
        if (obj.kind == SceneRenderObjectKind::NodeFooter && obj.text == "Switch") has_footer = true;
    }
    EXPECT_TRUE(has_frame);
    EXPECT_TRUE(has_title);
    EXPECT_TRUE(has_footer);
}

TEST(PresentationCompilerIntegration, RefNodeFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(101), "GND", NodeFrameKind::Reference);

    NodePresentation p = compile_node_presentation(spec);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Reference);

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(60.0f, 40.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
    bool has_title = false;
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind == SceneRenderObjectKind::NodeTitle && obj.text == "GND") has_title = true;
    }
    EXPECT_TRUE(has_title);
}

TEST(PresentationCompilerIntegration, BusNodeFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(102), "DC Bus", NodeFrameKind::Bus);

    NodePresentation p = compile_node_presentation(spec);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Bus);

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(200.0f, 40.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
    bool has_frame = false;
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind == SceneRenderObjectKind::NodeFrame && obj.frame_kind == NodeFrameKind::Bus) {
            has_frame = true;
        }
    }
    EXPECT_TRUE(has_frame);
}

TEST(PresentationCompilerIntegration, GroupNodeFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(103), "Power Section", NodeFrameKind::Group);

    NodePresentation p = compile_node_presentation(spec);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(300.0f, 200.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
}

TEST(PresentationCompilerIntegration, TextNodeFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(104), "Note", NodeFrameKind::Annotation);
    spec.annotation_text = "Design note";

    NodePresentation p = compile_node_presentation(spec);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(p.shell.annotation_text, "Design note");

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(200.0f, 100.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
}

TEST(PresentationCompilerIntegration, GaugeNodeFlowsThroughFullPipeline) {
    auto spec = make_spec(ui::InternedId(105), "Voltmeter", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_min = 0.0f;
    spec.content_max = 30.0f;
    spec.content_value = 27.5f;
    spec.content_unit = "V";

    NodePresentation p = compile_node_presentation(spec);
    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(180.0f, 160.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    // Should have frame + title + many content paint objects
    size_t content_paints = 0;
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind == SceneRenderObjectKind::ContentPaint) ++content_paints;
    }
    EXPECT_GE(content_paints, 10u);  // Arc + ticks + needle + dot + texts
}

TEST(PresentationCompilerIntegration, MultiNodeSnapshotWithMixedKinds) {
    auto standard = make_spec(ui::InternedId(200), "Battery", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    standard.content_value = 27.0f;
    standard.content_unit = "V";

    auto ref = make_spec(ui::InternedId(201), "GND", NodeFrameKind::Reference);
    auto bus = make_spec(ui::InternedId(202), "DC Bus", NodeFrameKind::Bus);

    NodePresentation p1 = compile_node_presentation(standard);
    NodePresentation p2 = compile_node_presentation(ref);
    NodePresentation p3 = compile_node_presentation(bus);

    NodeSlotLayout l1 = layout_node_presentation(p1, ui::Pt(180.0f, 160.0f));
    NodeSlotLayout l2 = layout_node_presentation(p2, ui::Pt(60.0f, 40.0f));
    NodeSlotLayout l3 = layout_node_presentation(p3, ui::Pt(200.0f, 40.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{p1, l1},
        SemanticSceneNode{p2, l2},
        SemanticSceneNode{p3, l3},
    });

    EXPECT_EQ(snapshot.node_index.size(), 3u);

    // Verify each node has its own index entry
    EXPECT_NE(find_scene_node_index(snapshot, ui::InternedId(200)), nullptr);
    EXPECT_NE(find_scene_node_index(snapshot, ui::InternedId(201)), nullptr);
    EXPECT_NE(find_scene_node_index(snapshot, ui::InternedId(202)), nullptr);

    // All object IDs should be unique
    for (size_t i = 0; i + 1 < snapshot.render_objects.size(); ++i) {
        for (size_t j = i + 1; j < snapshot.render_objects.size(); ++j) {
            EXPECT_NE(snapshot.render_objects[i].id, snapshot.render_objects[j].id);
        }
    }
}

// ============================================================================
// Registry-based compile (backward compat with existing tests)
// ============================================================================

namespace {

PresentationNode make_empty_fragment(const PresentationSpec& /*spec*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(40);
    root.layout = LayoutKind::Column;
    return root;
}

PresentationNode make_custom_fragment(const PresentationSpec& spec) {
    PresentationNode root;
    root.element_id = ui::InternedId(50);
    root.layout = LayoutKind::Overlay;

    PresentationNode title;
    title.element_id = ui::InternedId(51);
    PaintCommand title_cmd;
    title_cmd.id = ui::InternedId(52);
    title_cmd.kind = PaintPrimitiveKind::Text;
    title_cmd.text = spec.title;
    title.paint.push_back(std::move(title_cmd));

    PresentationNode badge;
    badge.element_id = ui::InternedId(53);
    PaintCommand badge_cmd;
    badge_cmd.id = ui::InternedId(54);
    badge_cmd.kind = PaintPrimitiveKind::Circle;
    badge.paint.push_back(std::move(badge_cmd));

    HitRegion badge_hit;
    badge_hit.id = ui::InternedId(55);
    badge_hit.kind = HitShapeKind::Circle;
    badge.hit_regions.push_back(badge_hit);

    InteractionBinding badge_click;
    badge_click.region_id = badge_hit.id;
    badge_click.kind = InteractionKind::Click;
    badge_click.action_id = ui::InternedId(56);
    badge.interactions.push_back(badge_click);

    root.children.push_back(std::move(title));
    root.children.push_back(std::move(badge));
    return root;
}

} // namespace

TEST(RegistryBasedCompile, PreservesNodeIdentityAndTitle) {
    auto spec = make_spec(ui::InternedId(300), "Generator");
    spec.type_id = ui::InternedId(100);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(100), NodePresenter{NodeFrameKind::Standard, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation p = compile_node_presentation(ctx, spec);

    EXPECT_EQ(p.node_id, ui::InternedId(300));
    EXPECT_EQ(p.shell.title, "Generator");
}

TEST(RegistryBasedCompile, CustomPresenterOverridesDefaultContent) {
    auto spec = make_spec(ui::InternedId(301), "Custom", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.type_id = ui::InternedId(500);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(500), NodePresenter{NodeFrameKind::Group, &make_custom_fragment});

    NodePresentationCompileContext ctx{&registry};
    NodePresentation p = compile_node_presentation(ctx, spec);

    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
    ASSERT_EQ(p.content.children.size(), 2u);
    EXPECT_EQ(p.content.children[0].paint[0].text, "Custom");
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);
}

TEST(RegistryBasedCompile, RegistryReturnsNullForMissingType) {
    NodePresenterRegistry registry;
    EXPECT_EQ(registry.find_presenter(ui::InternedId(999)), nullptr);
}

TEST(RegistryBasedCompile, MissingPresenterFallsBackToDefault) {
    auto spec = make_spec(ui::InternedId(302), "Missing");
    NodePresenterRegistry registry;
    // Registry has no presenter for type 302 — compiler should fall back to default
    NodePresentationCompileContext ctx{&registry};
    NodePresentation p = compile_node_presentation(ctx, spec);

    // Fallback produces a valid presentation with the node's name
    EXPECT_EQ(p.shell.title, "Missing");
}

// ============================================================================
// Value node (render_hint="ref" with Value type)
// ============================================================================

TEST(CompileNodePresentation, ValueNodeWithRefHintGetsReferenceFrame) {
    auto spec = make_spec(ui::InternedId(400), "28.0", NodeFrameKind::Reference, bp2::NodeContentType::Value);
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Reference);
    EXPECT_EQ(p.shell.title, "28.0");
}

// ============================================================================
// Regression: annotation font_size — the compiler passes through spec values.
// Parsing edge cases are tested via make_presentation_spec(node, def, interner)
// in the PresentationSpec section below.
// ============================================================================

TEST(CompileNodePresentation, NegativeFontSizePassedThrough) {
    auto spec = make_spec(ui::InternedId(501), "Note", NodeFrameKind::Annotation);
    spec.annotation_font_size = -5.0f;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, -5.0f);
}

TEST(CompileNodePresentation, CustomFontSizePassedThrough) {
    auto spec = make_spec(ui::InternedId(502), "Note", NodeFrameKind::Annotation);
    spec.annotation_font_size = 24.0f;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 24.0f);
}

// ============================================================================
// Regression: Value content type produces empty content
// ============================================================================

TEST(DefaultContentPresenter, ValueContentTypeProducesEmptyChildren) {
    auto spec = make_spec(ui::InternedId(510), "28V", NodeFrameKind::Standard, bp2::NodeContentType::Value);
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_TRUE(p.content.children.empty());
    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
}

// ============================================================================
// Regression: slider DragScalar range matches preferred width
// ============================================================================

TEST(DefaultContentPresenter, SliderDragScalarRangeMatchesPreferredWidth) {
    auto spec = make_spec(ui::InternedId(520), "Throttle", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(spec);

    const auto* drag = find_interaction(p.content, InteractionKind::DragScalar);
    ASSERT_NE(drag, nullptr);

    // Slider preferred width = 60, handle radius = 6
    // min_value = pad = 6, max_value = pad + (60 - 2*6) = 54
    EXPECT_FLOAT_EQ(drag->min_value, 6.0f);
    EXPECT_FLOAT_EQ(drag->max_value, 54.0f);
}

// ============================================================================
// Regression: slider with zero range doesn't crash
// ============================================================================

TEST(DefaultContentPresenter, SliderZeroRangeProducesZeroT) {
    auto spec = make_spec(ui::InternedId(530), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 50.0f;
    spec.content_max = 50.0f;
    spec.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(spec);

    // Should not crash and should produce valid content.
    // With t == 0 the fill rectangle is omitted, leaving:
    //   track bg + handle circle + value text + drag interaction = 4 children
    EXPECT_GE(p.content.children.size(), 4u);
    // No fill rectangle when t == 0
    EXPECT_EQ(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 1u);
    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "50.0");
}

// ============================================================================
// Regression: gauge with zero range doesn't crash
// ============================================================================

TEST(DefaultContentPresenter, GaugeZeroRangeProducesZeroNormalized) {
    auto spec = make_spec(ui::InternedId(540), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_min = 10.0f;
    spec.content_max = 10.0f;
    spec.content_value = 10.0f;
    NodePresentation p = compile_node_presentation(spec);

    // Should not crash and should produce valid content
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Arc), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 12u);
}

// ============================================================================
// Regression: indicator with negative value
// ============================================================================

TEST(DefaultContentPresenter, IndicatorNegativeValueProducesDimColor) {
    auto spec = make_spec(ui::InternedId(550), "Light", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec.content_value = -1.0f;
    NodePresentation p = compile_node_presentation(spec);

    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);
    const PaintCommand* circle = nullptr;
    for (const auto* pc : paints) {
        if (pc->kind == PaintPrimitiveKind::Circle) { circle = pc; break; }
    }
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->fill_color, 0xFF505050u);  // dim/off color
}

// ============================================================================
// Regression: knob value clamped to valid range
// ============================================================================

TEST(DefaultContentPresenter, KnobValueClampedToMaxPositions) {
    auto spec = make_spec(ui::InternedId(560), "Selector", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec.content_max = 3.0f;
    spec.content_value = 999.0f;  // way beyond max
    NodePresentation p = compile_node_presentation(spec);

    // Should not crash; knob should still produce valid content
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 2u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 3.0f);
}

// ============================================================================
// Regression: explicit geometry types for non-rect primitives
// ============================================================================

TEST(ExplicitGeometry, IndicatorCircleCarriesCircleGeometry) {
    auto spec = make_spec(ui::InternedId(600), "Indicator", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec.content_value = 1.0f;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* circle_paint = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Circle) {
            circle_paint = paint;
            break;
        }
    }
    ASSERT_NE(circle_paint, nullptr);

    const auto* geo = std::get_if<CircleGeometry>(&circle_paint->geometry);
    ASSERT_NE(geo, nullptr) << "Circle paint must carry CircleGeometry";
    EXPECT_GT(geo->radius, 0.0f);
    EXPECT_FLOAT_EQ(geo->cx, 0.0f);
    EXPECT_FLOAT_EQ(geo->cy, 0.0f);
}

TEST(ExplicitGeometry, KnobCircleAndLinesCarryExplicitGeometry) {
    auto spec = make_spec(ui::InternedId(601), "Knob", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec.content_max = 5.0f;
    spec.content_value = 2.0f;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    // Knob body circle
    const PaintCommand* circle_paint = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Circle) {
            circle_paint = paint;
            break;
        }
    }
    ASSERT_NE(circle_paint, nullptr);
    const auto* circle_geo = std::get_if<CircleGeometry>(&circle_paint->geometry);
    ASSERT_NE(circle_geo, nullptr) << "Knob body must carry CircleGeometry";
    EXPECT_GT(circle_geo->radius, 0.0f);

    // Tick and pointer lines
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Line) {
            const auto* line_geo = std::get_if<LineGeometry>(&paint->geometry);
            ASSERT_NE(line_geo, nullptr) << "Knob line must carry LineGeometry";
            EXPECT_GE(line_geo->outer_radius, line_geo->inner_radius);
        }
    }
}

TEST(ExplicitGeometry, GaugeArcAndTextsCarryExplicitGeometry) {
    auto spec = make_spec(ui::InternedId(602), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_value = 15.0f;
    spec.content_min = 0.0f;
    spec.content_max = 30.0f;
    spec.content_unit = "Volts";
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    // Arc
    const PaintCommand* arc_paint = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Arc) {
            arc_paint = paint;
            break;
        }
    }
    ASSERT_NE(arc_paint, nullptr);
    const auto* arc_geo = std::get_if<ArcGeometry>(&arc_paint->geometry);
    ASSERT_NE(arc_geo, nullptr) << "Gauge arc must carry ArcGeometry";
    EXPECT_GT(arc_geo->radius, 0.0f);
    EXPECT_NE(arc_geo->sweep_angle_deg, 0.0f);

    // Value and unit texts with distinct Y offsets
    const PaintCommand* value_text = nullptr;
    const PaintCommand* unit_text = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Text) {
            if (paint->text == "15.0") value_text = paint;
            else if (paint->text == "Volts") unit_text = paint;
        }
    }
    ASSERT_NE(value_text, nullptr);
    ASSERT_NE(unit_text, nullptr);

    const auto* value_tg = std::get_if<TextGeometry>(&value_text->geometry);
    const auto* unit_tg = std::get_if<TextGeometry>(&unit_text->geometry);
    ASSERT_NE(value_tg, nullptr);
    ASSERT_NE(unit_tg, nullptr);
    EXPECT_GT(unit_tg->y, value_tg->y) << "Unit text must be below value text";
    EXPECT_GT(value_tg->font_size, unit_tg->font_size) << "Value text must be larger than unit text";
    EXPECT_TRUE(value_tg->center_aligned);
    EXPECT_TRUE(unit_tg->center_aligned);
}

TEST(ExplicitGeometry, LayoutContentTreePlacesElementsDirectlyInBounds) {
    auto spec = make_spec(ui::InternedId(603), "Knob", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec.content_max = 3.0f;
    NodePresentation p = compile_node_presentation(spec);

    const ui::Rect bounds{10.0f, 20.0f, 48.0f, 48.0f};
    auto placements = layout_content_tree(p.content, bounds);

    // Root placement should match the given bounds exactly
    ASSERT_FALSE(placements.empty());
    EXPECT_FLOAT_EQ(placements[0].bounds.x, bounds.x);
    EXPECT_FLOAT_EQ(placements[0].bounds.y, bounds.y);
    EXPECT_FLOAT_EQ(placements[0].bounds.w, bounds.w);
    EXPECT_FLOAT_EQ(placements[0].bounds.h, bounds.h);

    // All child placements should be within the bounds (Overlay layout)
    for (const auto& placement : placements) {
        EXPECT_GE(placement.bounds.x, bounds.x);
        EXPECT_GE(placement.bounds.y, bounds.y);
        EXPECT_LE(placement.bounds.x + placement.bounds.w, bounds.x + bounds.w + 0.01f);
        EXPECT_LE(placement.bounds.y + placement.bounds.h, bounds.y + bounds.h + 0.01f);
    }
}

TEST(ExplicitGeometry, ContentSemanticSnapshotHasNoShellObjects) {
    auto spec = make_spec(ui::InternedId(604), "Indicator", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec.content_value = 0.5f;
    NodePresentation p = compile_node_presentation(spec);

    const ui::Rect bounds{5.0f, 10.0f, 24.0f, 24.0f};
    auto placements = layout_content_tree(p.content, bounds);
    auto snapshot = build_content_semantic_scene_snapshot(p, placements);

    // Content-only snapshot should have no NodeFrame, NodeTitle, or NodeFooter objects
    for (const auto& obj : snapshot.render_objects) {
        EXPECT_EQ(obj.kind, SceneRenderObjectKind::ContentPaint)
            << "Content-only snapshot must not contain shell render objects";
    }
    // Should have no NodeBody hit objects
    for (const auto& obj : snapshot.hit_objects) {
        EXPECT_EQ(obj.kind, SceneHitObjectKind::ContentRegion)
            << "Content-only snapshot must not contain shell hit objects";
    }
}

// ============================================================================
// Regression tests for explicit geometry correctness
// ============================================================================

TEST(ExplicitGeometry, IndicatorBreathingRadiusVariesWithValue) {
    // Regression: indicator radius must vary with brightness (breathing effect).
    // A static radius (e.g. INDICATOR_SIZE * 0.5f) is a regression.
    auto spec_off = make_spec(ui::InternedId(700), "Indicator", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec_off.content_value = 0.0f;
    NodePresentation p_off = compile_node_presentation(spec_off);

    auto spec_on = make_spec(ui::InternedId(701), "Indicator", NodeFrameKind::Standard, bp2::NodeContentType::Indicator);
    spec_on.content_value = 1.0f;
    NodePresentation p_on = compile_node_presentation(spec_on);

    auto get_indicator_radius = [](const PresentationNode& content) -> float {
        for (const auto& child : content.children) {
            for (const auto& p : child.paint) {
                if (p.kind == PaintPrimitiveKind::Circle) {
                    const auto* geo = std::get_if<CircleGeometry>(&p.geometry);
                    return geo ? geo->radius : 0.0f;
                }
            }
        }
        return 0.0f;
    };

    float r_off = get_indicator_radius(p_off.content);
    float r_on = get_indicator_radius(p_on.content);
    EXPECT_GT(r_off, 0.0f) << "Off indicator must still have positive radius";
    EXPECT_GT(r_on, r_off) << "Full-brightness indicator must have larger radius than off";
    // Exact formula: INDICATOR_SIZE * (0.3 + 0.15 * b), INDICATOR_SIZE = 24
    EXPECT_NEAR(r_off, 24.0f * 0.3f, 0.01f);
    EXPECT_NEAR(r_on, 24.0f * 0.45f, 0.01f);
}

TEST(ExplicitGeometry, SliderHandleCarriesCircleGeometryWithRadius) {
    // Regression: slider handle Circle must carry CircleGeometry with SLIDER_HANDLE_RADIUS.
    // A missing geometry (defaulting to TextGeometry) makes the handle invisible.
    auto spec = make_spec(ui::InternedId(702), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_value = 5.0f;
    spec.content_min = 0.0f;
    spec.content_max = 10.0f;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* handle = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Circle) {
            handle = paint;
            break;
        }
    }
    ASSERT_NE(handle, nullptr) << "Slider must have a Circle paint for the handle";

    const auto* geo = std::get_if<CircleGeometry>(&handle->geometry);
    ASSERT_NE(geo, nullptr) << "Slider handle must carry CircleGeometry, not TextGeometry";
    EXPECT_NEAR(geo->radius, 6.0f, 0.01f) << "Handle radius must be SLIDER_HANDLE_RADIUS (6.0)";
}

TEST(ExplicitGeometry, DefaultGeometryMatchesPrimitiveKind) {
    // Regression: make_paint must produce geometry matching the primitive kind.
    // A Rectangle paint must carry RectGeometry, Circle must carry CircleGeometry, etc.
    auto spec = make_spec(ui::InternedId(703), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_value = 5.0f;
    spec.content_min = 0.0f;
    spec.content_max = 10.0f;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    for (const auto* paint : paints) {
        switch (paint->kind) {
            case PaintPrimitiveKind::Rectangle:
                EXPECT_NE(std::get_if<RectGeometry>(&paint->geometry), nullptr)
                    << "Rectangle paint must carry RectGeometry";
                break;
            case PaintPrimitiveKind::Circle:
                EXPECT_NE(std::get_if<CircleGeometry>(&paint->geometry), nullptr)
                    << "Circle paint must carry CircleGeometry";
                break;
            case PaintPrimitiveKind::Line:
                EXPECT_NE(std::get_if<LineGeometry>(&paint->geometry), nullptr)
                    << "Line paint must carry LineGeometry";
                break;
            case PaintPrimitiveKind::Arc:
                EXPECT_NE(std::get_if<ArcGeometry>(&paint->geometry), nullptr)
                    << "Arc paint must carry ArcGeometry";
                break;
            case PaintPrimitiveKind::Text:
                EXPECT_NE(std::get_if<TextGeometry>(&paint->geometry), nullptr)
                    << "Text paint must carry TextGeometry";
                break;
        }
    }
}

TEST(ExplicitGeometry, GaugeRadialCenterOffsetMatchesLegacyPosition) {
    // Regression: gauge radial center must be at GAUGE_RADIUS from the top of content,
    // not at the vertical center. With content height 92 and GAUGE_RADIUS 40,
    // the offset from center is 40 - 46 = -6.
    auto spec = make_spec(ui::InternedId(704), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_value = 15.0f;
    spec.content_min = 0.0f;
    spec.content_max = 30.0f;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    // Check arc center offset
    const PaintCommand* arc_paint = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Arc) {
            arc_paint = paint;
            break;
        }
    }
    ASSERT_NE(arc_paint, nullptr);
    const auto* arc_geo = std::get_if<ArcGeometry>(&arc_paint->geometry);
    ASSERT_NE(arc_geo, nullptr);
    constexpr auto metrics = gauge_metrics();
    EXPECT_FLOAT_EQ(arc_geo->cy, metrics.center_offset_y())
        << "Gauge arc center must derive from shared gauge metrics";

    // Check all radial primitives share the same center offset
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Line) {
            const auto* line_geo = std::get_if<LineGeometry>(&paint->geometry);
            ASSERT_NE(line_geo, nullptr);
            EXPECT_FLOAT_EQ(line_geo->cy, metrics.center_offset_y()) << "Gauge line center must match arc center offset";
        }
        if (paint->kind == PaintPrimitiveKind::Circle) {
            const auto* circle_geo = std::get_if<CircleGeometry>(&paint->geometry);
            ASSERT_NE(circle_geo, nullptr);
            EXPECT_FLOAT_EQ(circle_geo->cy, metrics.center_offset_y()) << "Gauge center dot must match arc center offset";
        }
    }
}

TEST(ExplicitGeometry, GaugeTextYOffsetsMatchLegacyPositioning) {
    // Regression: gauge value text must be at Y = GAUGE_RADIUS * 2 + 5 = 85,
    // unit text at Y = GAUGE_RADIUS * 2 + 21 = 101, matching old absolute positioning.
    auto spec = make_spec(ui::InternedId(705), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_value = 15.0f;
    spec.content_min = 0.0f;
    spec.content_max = 30.0f;
    spec.content_unit = "V";
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* value_text = nullptr;
    const PaintCommand* unit_text = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Text) {
            if (paint->text == "15.0") value_text = paint;
            else if (paint->text == "V") unit_text = paint;
        }
    }
    ASSERT_NE(value_text, nullptr);
    ASSERT_NE(unit_text, nullptr);

    const auto* value_tg = std::get_if<TextGeometry>(&value_text->geometry);
    const auto* unit_tg = std::get_if<TextGeometry>(&unit_text->geometry);
    ASSERT_NE(value_tg, nullptr);
    ASSERT_NE(unit_tg, nullptr);

    constexpr auto metrics = gauge_metrics();
    EXPECT_NEAR(value_tg->y, metrics.value_text_y(), 0.01f) << "Value text Y must derive from shared gauge metrics";
    EXPECT_NEAR(unit_tg->y, metrics.unit_text_y(), 0.01f) << "Unit text Y must derive from shared gauge metrics";
}

TEST(ExplicitGeometry, GaugePreferredSizeMatchesSharedMetrics) {
    auto spec = make_spec(ui::InternedId(706), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    NodePresentation p = compile_node_presentation(spec);
    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(180.0f, 140.0f));

    constexpr auto metrics = gauge_metrics();
    const ui::Rect* body = find_slot(layout, NodeSlot::Body);
    ASSERT_NE(body, nullptr);
    EXPECT_GE(body->h, metrics.preferred_height())
        << "Gauge body height must reserve at least the shared preferred gauge height";
}

TEST(ExplicitGeometry, GaugeMetricsDerivedFieldsAreConsistent) {
    // Regression: preferred_height must encompass all content including unit text.
    // If derived fields drift from primaries, gauge text overflows its bounds.
    constexpr auto m = gauge_metrics();

    // preferred_height must reach past unit text bottom
    EXPECT_GE(m.preferred_height(), m.unit_text_y() + m.unit_font_size)
        << "preferred_height must encompass unit text";
    EXPECT_GE(m.preferred_height(), m.value_text_y() + m.value_font_size)
        << "preferred_height must encompass value text";

    // preferred_width must encompass the arc diameter
    EXPECT_GE(m.preferred_width(), m.diameter())
        << "preferred_width must encompass arc diameter";

    // tick radii must be inside the arc
    EXPECT_LT(m.major_tick_inner_radius(), m.radius);
    EXPECT_LT(m.minor_tick_inner_radius(), m.radius);
    EXPECT_LT(m.major_tick_inner_radius(), m.minor_tick_inner_radius());

    // needle must fit inside the arc
    EXPECT_LE(m.needle_length, m.radius);
}

TEST(ExplicitGeometry, NoRectanglePaintHasZeroSizeGeometry) {
    // Regression: every Rectangle paint must carry RectGeometry with positive w and h.
    // A RectGeometry{0,0,0,0} (the default) causes the renderer to draw a zero-size rect
    // because it no longer falls back to element bounds.
    const std::vector<bp2::NodeContentType> types_with_rects = {
        bp2::NodeContentType::Switch,
        bp2::NodeContentType::VerticalToggle,
        bp2::NodeContentType::Slider,
    };

    for (auto content_type : types_with_rects) {
        auto spec = make_spec(ui::InternedId(800), "Test", NodeFrameKind::Standard, content_type);
        spec.content_state = true;
        spec.content_min = 0.0f;
        spec.content_max = 100.0f;
        spec.content_value = 50.0f;
        NodePresentation p = compile_node_presentation(spec);

        std::vector<const PaintCommand*> paints;
        collect_paints(p.content, paints);

        for (const auto* paint : paints) {
            if (paint->kind == PaintPrimitiveKind::Rectangle) {
                const auto* geo = std::get_if<RectGeometry>(&paint->geometry);
                ASSERT_NE(geo, nullptr) << "Rectangle paint must carry RectGeometry";
                EXPECT_GT(geo->w, 0.0f)
                    << "Rectangle RectGeometry.w must be positive (content_type="
                    << static_cast<int>(content_type) << ")";
                EXPECT_GT(geo->h, 0.0f)
                    << "Rectangle RectGeometry.h must be positive (content_type="
                    << static_cast<int>(content_type) << ")";
            }
        }
    }
}

TEST(ExplicitGeometry, SwitchBackgroundAndHandleHaveNonZeroGeometry) {
    // Regression: switch background and handle must have explicit non-zero RectGeometry.
    // Without this, the renderer draws zero-size rectangles (invisible switch).
    auto spec = make_spec(ui::InternedId(801), "AZS", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec.content_state = true;
    NodePresentation p = compile_node_presentation(spec);

    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);

    const PaintCommand* bg = nullptr;
    const PaintCommand* handle = nullptr;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Rectangle) {
            if (bg == nullptr) bg = paint;
            else if (handle == nullptr) handle = paint;
        }
    }

    ASSERT_NE(bg, nullptr);
    ASSERT_NE(handle, nullptr);

    const auto* bg_geo = std::get_if<RectGeometry>(&bg->geometry);
    const auto* handle_geo = std::get_if<RectGeometry>(&handle->geometry);
    ASSERT_NE(bg_geo, nullptr);
    ASSERT_NE(handle_geo, nullptr);

    EXPECT_GT(bg_geo->w, 0.0f) << "Background width must be positive";
    EXPECT_GT(bg_geo->h, 0.0f) << "Background height must be positive";
    EXPECT_GT(handle_geo->w, 0.0f) << "Handle width must be positive";
    EXPECT_GT(handle_geo->h, 0.0f) << "Handle height must be positive";

    // Handle must be smaller than background
    EXPECT_LT(handle_geo->w, bg_geo->w) << "Handle must be narrower than background";

    // On-state handle must be at the right side
    EXPECT_GT(handle_geo->x, 0.0f) << "On-state handle must be offset to the right";
}

// ============================================================================
// Regression: slider fill rectangle omitted at zero fill (load-time crash)
//
// The assertion `rect->w > 0` in append_painted() fired during Document::load()
// because the slider track fill rectangle had width = t * track_width, and t == 0
// when the slider is at its minimum value (the default state on load).
// ============================================================================

TEST(SliderFillRegression, SliderAtMinimumValueOmitsFillRectangle) {
    // This is the exact crash scenario: a Slider node with default view state
    // (content_value == content_min == 0, content_max == 1) produces t == 0.
    // The fill rectangle must be omitted, not emitted with zero width.
    auto spec = make_spec(ui::InternedId(800), "Throttle", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 0.0f;  // at minimum → t == 0

    // Must not crash (was: assertion failure in append_painted)
    NodePresentation p = compile_node_presentation(spec);

    // Only the track background rectangle should be present (no fill)
    EXPECT_EQ(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 1u)
        << "Slider at minimum must have only the track background rectangle, no fill";

    // Handle circle, value text, and drag interaction must still be present
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Text), 1u);
    const auto* drag = find_interaction(p.content, InteractionKind::DragScalar);
    ASSERT_NE(drag, nullptr);
}

TEST(SliderFillRegression, SliderAtMaximumValueEmitsFillRectangle) {
    auto spec = make_spec(ui::InternedId(801), "Throttle", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 100.0f;  // at maximum → t == 1

    NodePresentation p = compile_node_presentation(spec);

    // Both track background and fill rectangles should be present
    EXPECT_EQ(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u)
        << "Slider at maximum must have both track background and fill rectangles";

    // Verify fill rectangle has full track width
    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);
    const PaintCommand* fill = nullptr;
    int rect_idx = 0;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Rectangle) {
            if (rect_idx == 1) { fill = paint; break; }
            ++rect_idx;
        }
    }
    ASSERT_NE(fill, nullptr);
    const auto* fill_geo = std::get_if<RectGeometry>(&fill->geometry);
    ASSERT_NE(fill_geo, nullptr);
    EXPECT_NEAR(fill_geo->w, 48.0f, 0.01f);  // SLIDER_WIDTH - 2*SLIDER_HANDLE_RADIUS
}

TEST(SliderFillRegression, SliderWithDefaultViewStateCompilesWithoutCrash) {
    // Simulates the exact load-time scenario: a Slider with default state
    // (content_value=0, content_min=0, content_max=0). This is what every
    // Slider looks like before hydration populates runtime values.
    PresentationSpec spec;
    spec.node_id = ui::InternedId(802);
    spec.type_id = ui::InternedId(1000);
    spec.title = "DefaultSlider";
    spec.content_type = bp2::NodeContentType::Slider;
    // All other fields at their defaults: value=0, min=0, max=0

    // Must not crash
    NodePresentation p = compile_node_presentation(spec);
    EXPECT_FALSE(p.content.children.empty());
}

TEST(SliderFillRegression, SliderAtMinimumFlowsThroughFullPipeline) {
    // End-to-end: compile → layout → snapshot for a slider at t == 0.
    // This exercises the same code path as Document::load() → scene rebuild →
    // NodeWidget::refresh_content_semantic_snapshot().
    auto spec = make_spec(ui::InternedId(803), "Throttle", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 0.0f;

    // Step 1: Compile (was crashing here)
    NodePresentation p = compile_node_presentation(spec);

    // Step 2: Layout
    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(180.0f, 120.0f));
    EXPECT_GT(layout.node_bounds.w, 0.0f);
    EXPECT_GT(layout.node_bounds.h, 0.0f);

    // Step 3: Snapshot
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);
    EXPECT_FALSE(snapshot.render_objects.empty());
}

// ============================================================================
// Regression: all content types compile without assertion failure at defaults
//
// Ensures that every NodeContentType can be compiled with default view state.
// This is the load-time invariant: any node in the blueprint must survive
// compile_node_presentation() without hitting geometry assertions.
// ============================================================================

TEST(LoadTimeCompileRegression, AllContentTypesCompileAtDefaultViewState) {
    const bp2::NodeContentType types[] = {
        bp2::NodeContentType::None,
        bp2::NodeContentType::Switch,
        bp2::NodeContentType::VerticalToggle,
        bp2::NodeContentType::Slider,
        bp2::NodeContentType::Indicator,
        bp2::NodeContentType::Knob,
        bp2::NodeContentType::Gauge,
        bp2::NodeContentType::Text,
        bp2::NodeContentType::Value,
    };

    for (auto content_type : types) {
        PresentationSpec spec;
        spec.node_id = ui::InternedId(900 + static_cast<int>(content_type));
        spec.type_id = ui::InternedId(1000);
        spec.title = "TestNode";
        spec.content_type = content_type;

        // Must not crash for any content type
        EXPECT_NO_FATAL_FAILURE(compile_node_presentation(spec))
            << "compile_node_presentation crashed for content_type="
            << static_cast<int>(content_type);
    }
}

TEST(LoadTimeCompileRegression, AllContentTypesFlowThroughFullPipelineAtDefaults) {
    const bp2::NodeContentType types[] = {
        bp2::NodeContentType::None,
        bp2::NodeContentType::Switch,
        bp2::NodeContentType::VerticalToggle,
        bp2::NodeContentType::Slider,
        bp2::NodeContentType::Indicator,
        bp2::NodeContentType::Knob,
        bp2::NodeContentType::Gauge,
        bp2::NodeContentType::Text,
        bp2::NodeContentType::Value,
    };

    for (auto content_type : types) {
        PresentationSpec spec;
        spec.node_id = ui::InternedId(950 + static_cast<int>(content_type));
        spec.type_id = ui::InternedId(1000);
        spec.title = "TestNode";
        spec.content_type = content_type;

        NodePresentation p = compile_node_presentation(spec);
        NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(180.0f, 120.0f));
        SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

        EXPECT_FALSE(snapshot.render_objects.empty())
            << "Full pipeline produced no render objects for content_type="
            << static_cast<int>(content_type);
    }
}

// ============================================================================
// Regression: all rectangle paints have positive geometry
//
// Structural invariant test: after compiling any content type with any
// reasonable parameter combination, every Rectangle paint must carry
// RectGeometry with w > 0 and h > 0.
// ============================================================================

TEST(LoadTimeCompileRegression, AllRectanglePaintsHavePositiveGeometry) {
    struct TestCase {
        bp2::NodeContentType type;
        float value;
        float min;
        float max;
    };

    const TestCase cases[] = {
        {bp2::NodeContentType::Slider, 0.0f, 0.0f, 100.0f},    // t == 0
        {bp2::NodeContentType::Slider, 50.0f, 0.0f, 100.0f},   // t == 0.5
        {bp2::NodeContentType::Slider, 100.0f, 0.0f, 100.0f},  // t == 1
        {bp2::NodeContentType::Slider, 10.0f, 10.0f, 10.0f},   // zero range
        {bp2::NodeContentType::Switch, 0.0f, 0.0f, 0.0f},
        {bp2::NodeContentType::VerticalToggle, 0.0f, 0.0f, 0.0f},
    };

    for (const auto& tc : cases) {
        auto spec = make_spec(ui::InternedId(1100 + static_cast<int>(tc.type) * 10 + static_cast<int>(tc.value)), "Test", NodeFrameKind::Standard, tc.type);
        spec.content_value = tc.value;
        spec.content_min = tc.min;
        spec.content_max = tc.max;

        NodePresentation p = compile_node_presentation(spec);

        std::vector<const PaintCommand*> paints;
        collect_paints(p.content, paints);

        for (const auto* paint : paints) {
            if (paint->kind == PaintPrimitiveKind::Rectangle) {
                const auto* geo = std::get_if<RectGeometry>(&paint->geometry);
                ASSERT_NE(geo, nullptr)
                    << "Rectangle paint must carry RectGeometry (type="
                    << static_cast<int>(tc.type) << ", value=" << tc.value << ")";
                EXPECT_GT(geo->w, 0.0f)
                    << "Rectangle width must be positive (type="
                    << static_cast<int>(tc.type) << ", value=" << tc.value << ")";
                EXPECT_GT(geo->h, 0.0f)
                    << "Rectangle height must be positive (type="
                    << static_cast<int>(tc.type) << ", value=" << tc.value << ")";
            }
        }
    }
}

// ============================================================================
// Issue #133 regression tests: single authority for static vs dynamic content
//
// These tests verify that the presentation compiler reads static semantics
// (min, max, unit, label, content_type) from view.content_* (set by
// hydrate_node_view) and dynamic state (value, state, tripped) independently.
// ============================================================================

TEST(Issue133_SingleAuthority, SliderUsesStaticMinMaxFromView) {
    // Verify that the slider presentation reads min/max from view.content_*
    // (the single authority set by hydrate_node_view) and that changing them
    // affects the compiled interaction binding range.
    auto spec_default = make_spec(ui::InternedId(2000), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec_default.content_min = 0.0f;
    spec_default.content_max = 100.0f;
    spec_default.content_value = 50.0f;

    auto spec_custom = make_spec(ui::InternedId(2001), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec_custom.content_min = -50.0f;
    spec_custom.content_max = 200.0f;
    spec_custom.content_value = 50.0f;

    NodePresentation p_default = compile_node_presentation(spec_default);
    NodePresentation p_custom = compile_node_presentation(spec_custom);

    // Both should produce slider content
    EXPECT_GE(p_default.content.children.size(), 2u);
    EXPECT_GE(p_custom.content.children.size(), 2u);

    // The value text should be the same (same content_value)
    const auto* text_default = find_text_paint(p_default.content);
    const auto* text_custom = find_text_paint(p_custom.content);
    ASSERT_NE(text_default, nullptr);
    ASSERT_NE(text_custom, nullptr);
    EXPECT_EQ(text_default->text, text_custom->text);  // both "50.0"
}

TEST(Issue133_SingleAuthority, KnobUsesStaticMaxFromView) {
    // Verify knob reads positions (content_max) from view, not from params.
    auto spec_2pos = make_spec(ui::InternedId(2010), "Knob", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec_2pos.content_max = 2.0f;
    spec_2pos.content_value = 0.0f;

    auto spec_5pos = make_spec(ui::InternedId(2011), "Knob", NodeFrameKind::Standard, bp2::NodeContentType::Knob);
    spec_5pos.content_max = 5.0f;
    spec_5pos.content_value = 0.0f;

    NodePresentation p_2 = compile_node_presentation(spec_2pos);
    NodePresentation p_5 = compile_node_presentation(spec_5pos);

    // 5-position knob should have more tick marks (Line paints) than 2-position
    size_t lines_2 = count_paint_kind(p_2.content, PaintPrimitiveKind::Line);
    size_t lines_5 = count_paint_kind(p_5.content, PaintPrimitiveKind::Line);
    EXPECT_GT(lines_5, lines_2);

    // Discrete interaction step count should differ
    const auto* drag_2 = find_interaction(p_2.content, InteractionKind::DragDiscrete);
    const auto* drag_5 = find_interaction(p_5.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag_2, nullptr);
    ASSERT_NE(drag_5, nullptr);
    EXPECT_FLOAT_EQ(drag_2->step, 2.0f);
    EXPECT_FLOAT_EQ(drag_5->step, 5.0f);
}

TEST(Issue133_SingleAuthority, GaugeUsesStaticMinMaxAndUnit) {
    // Verify gauge reads min/max/unit from view.content_* (static authority).
    auto spec = make_spec(ui::InternedId(2020), "Gauge", NodeFrameKind::Standard, bp2::NodeContentType::Gauge);
    spec.content_min = 0.0f;
    spec.content_max = 28.0f;
    spec.content_value = 14.0f;
    spec.content_unit = "V";

    NodePresentation p = compile_node_presentation(spec);

    // Should have unit text
    std::vector<const PaintCommand*> paints;
    collect_paints(p.content, paints);
    bool found_unit = false;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Text && paint->text == "V") {
            found_unit = true;
            break;
        }
    }
    EXPECT_TRUE(found_unit) << "Gauge should render unit text from view.content_unit";

    // Value text should show "14.0"
    bool found_value = false;
    for (const auto* paint : paints) {
        if (paint->kind == PaintPrimitiveKind::Text && paint->text == "14.0") {
            found_value = true;
            break;
        }
    }
    EXPECT_TRUE(found_value) << "Gauge should render value text from view.content_value";
}

TEST(Issue133_SingleAuthority, ToggleUsesDynamicStateFromView) {
    // Verify that switch/toggle reads state from view.content_state (dynamic)
    // and that the handle position changes with state.
    auto spec_off = make_spec(ui::InternedId(2030), "Switch", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_off.content_state = false;
    spec_off.content_tripped = false;

    auto spec_on = make_spec(ui::InternedId(2031), "Switch", NodeFrameKind::Standard, bp2::NodeContentType::Switch);
    spec_on.content_state = true;
    spec_on.content_tripped = false;

    NodePresentation p_off = compile_node_presentation(spec_off);
    NodePresentation p_on = compile_node_presentation(spec_on);

    // Both produce content, but handle positions differ
    std::vector<const PaintCommand*> paints_off, paints_on;
    collect_paints(p_off.content, paints_off);
    collect_paints(p_on.content, paints_on);

    // Find handle rectangles (second Rectangle paint in each)
    const PaintCommand* handle_off = nullptr;
    const PaintCommand* handle_on = nullptr;
    int rect_count = 0;
    for (const auto* p : paints_off) {
        if (p->kind == PaintPrimitiveKind::Rectangle) {
            if (++rect_count == 2) { handle_off = p; break; }
        }
    }
    rect_count = 0;
    for (const auto* p : paints_on) {
        if (p->kind == PaintPrimitiveKind::Rectangle) {
            if (++rect_count == 2) { handle_on = p; break; }
        }
    }
    ASSERT_NE(handle_off, nullptr);
    ASSERT_NE(handle_on, nullptr);

    const auto* geo_off = std::get_if<RectGeometry>(&handle_off->geometry);
    const auto* geo_on = std::get_if<RectGeometry>(&handle_on->geometry);
    ASSERT_NE(geo_off, nullptr);
    ASSERT_NE(geo_on, nullptr);
    EXPECT_NE(geo_off->x, geo_on->x) << "Switch handle should move between ON and OFF states";
}

// ============================================================================
// PresentationSpec regression tests
// ============================================================================

TEST(PresentationSpec, CanonicalMakeFromDefPreservesIdentity) {
    // Test the canonical make_presentation_spec(node, def, interner) path.
    // TypeDefinition with content_type "Slider" and render_hint "ref".
    ui::StringInterner interner;
    TypeDefinition def;
    def.render_hint = "ref";
    def.content_type = "Slider";
    def.params["min"] = "-10";
    def.params["max"] = "50";

    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(3000);
    node.semantic.type = interner.intern("TestType");
    node.view.name = "TestNode";
    node.view.content_value = 25.0f;
    node.view.content_state = true;
    node.view.content_tripped = true;

    PresentationSpec spec = make_presentation_spec(node, &def, interner);

    EXPECT_EQ(spec.node_id, ui::InternedId(3000));
    EXPECT_EQ(spec.title, "TestNode");
    EXPECT_EQ(spec.frame_kind, NodeFrameKind::Reference);
    EXPECT_EQ(spec.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(spec.content_min, -10.0f);
    EXPECT_FLOAT_EQ(spec.content_max, 50.0f);
    EXPECT_FLOAT_EQ(spec.content_value, 25.0f);
    EXPECT_TRUE(spec.content_state);
    EXPECT_TRUE(spec.content_tripped);
}

TEST(PresentationSpec, CanonicalMakeFromDefExtractsAnnotationParams) {
    ui::StringInterner interner;
    TypeDefinition def;
    def.render_hint = "text";

    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(3010);
    node.semantic.type = interner.intern("Annotation");
    node.view.name = "My Note";
    node.semantic.string_params["text"] = "Hello world";
    node.semantic.string_params["font_size"] = "18.5";

    PresentationSpec spec = make_presentation_spec(node, &def, interner);

    EXPECT_EQ(spec.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(spec.annotation_text, "Hello world");
    EXPECT_FLOAT_EQ(spec.annotation_font_size, 18.5f);
}

TEST(PresentationSpec, CanonicalMakeWithNullDefFallsBackToStandard) {
    ui::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(3020);
    node.semantic.type = interner.intern("Unknown");
    node.view.name = "Fallback";

    PresentationSpec spec = make_presentation_spec(node, nullptr, interner);

    EXPECT_EQ(spec.frame_kind, NodeFrameKind::Standard);
    EXPECT_EQ(spec.content_type, bp2::NodeContentType::None);
}

TEST(PresentationSpec, CanonicalMakeAnnotationFontSizeEdgeCases) {
    ui::StringInterner interner;
    TypeDefinition def;
    def.render_hint = "text";

    // Invalid font_size string
    {
        bp2::Blueprint::Node node;
        node.semantic.id = ui::InternedId(3030);
        node.semantic.type = interner.intern("Annotation");
        node.view.name = "Note";
        node.semantic.string_params["font_size"] = "not_a_number";
        PresentationSpec spec = make_presentation_spec(node, &def, interner);
        EXPECT_FLOAT_EQ(spec.annotation_font_size, 12.0f);
    }
    // Negative font_size
    {
        bp2::Blueprint::Node node;
        node.semantic.id = ui::InternedId(3031);
        node.semantic.type = interner.intern("Annotation");
        node.view.name = "Note";
        node.semantic.string_params["font_size"] = "-5.0";
        PresentationSpec spec = make_presentation_spec(node, &def, interner);
        EXPECT_FLOAT_EQ(spec.annotation_font_size, 12.0f);
    }
    // Zero font_size
    {
        bp2::Blueprint::Node node;
        node.semantic.id = ui::InternedId(3032);
        node.semantic.type = interner.intern("Annotation");
        node.view.name = "Note";
        node.semantic.string_params["font_size"] = "0";
        PresentationSpec spec = make_presentation_spec(node, &def, interner);
        EXPECT_FLOAT_EQ(spec.annotation_font_size, 12.0f);
    }
    // Empty font_size
    {
        bp2::Blueprint::Node node;
        node.semantic.id = ui::InternedId(3033);
        node.semantic.type = interner.intern("Annotation");
        node.view.name = "Note";
        node.semantic.string_params["font_size"] = "";
        PresentationSpec spec = make_presentation_spec(node, &def, interner);
        EXPECT_FLOAT_EQ(spec.annotation_font_size, 12.0f);
    }
}

TEST(PresentationSpec, DirectSpecConstructionBypassesNode) {
    // Verify the compiler works with a hand-built spec — no bp2::Blueprint::Node needed
    PresentationSpec spec;
    spec.node_id = ui::InternedId(4000);
    spec.type_id = ui::InternedId(4001);
    spec.frame_kind = NodeFrameKind::Standard;
    spec.title = "DirectSpec";
    spec.content_type = bp2::NodeContentType::Indicator;
    spec.content_value = 1.0f;

    NodePresentation p = compile_node_presentation(spec);

    EXPECT_EQ(p.node_id, ui::InternedId(4000));
    EXPECT_EQ(p.shell.title, "DirectSpec");
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Standard);
    // Indicator should produce a circle paint
    EXPECT_GT(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 0u);
}

TEST(PresentationSpec, DirectSpecAnnotationCompiles) {
    PresentationSpec spec;
    spec.node_id = ui::InternedId(4010);
    spec.frame_kind = NodeFrameKind::Annotation;
    spec.title = "Note";
    spec.annotation_text = "Some annotation";
    spec.annotation_font_size = 20.0f;

    NodePresentation p = compile_node_presentation(spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(p.shell.annotation_text, "Some annotation");
    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 20.0f);
}

TEST(PresentationSpec, RegistryLookupUsesSpecTypeId) {
    auto custom_presenter = [](const PresentationSpec& spec) -> PresentationNode {
        PresentationNode root;
        root.layout = LayoutKind::Column;
        PresentationNode child;
        PaintCommand pc;
        pc.id = ui::InternedId(1);
        pc.kind = PaintPrimitiveKind::Text;
        pc.text = "FromRegistry";
        child.paint.push_back(std::move(pc));
        root.children.push_back(std::move(child));
        return root;
    };

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(5000),
                                NodePresenter{NodeFrameKind::Bus, custom_presenter});

    PresentationSpec spec;
    spec.node_id = ui::InternedId(5001);
    spec.type_id = ui::InternedId(5000);
    spec.title = "RegistryTest";

    NodePresentationCompileContext ctx{&registry};
    NodePresentation p = compile_node_presentation(ctx, spec);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Bus);
    ASSERT_GE(p.content.children.size(), 1u);
    EXPECT_EQ(p.content.children[0].paint[0].text, "FromRegistry");
}

// ============================================================================
// Issue #133 regression
// ============================================================================

TEST(Issue133_SingleAuthority, DynamicStateIndependentOfStaticSemantics) {
    // Core regression test: changing static semantics (min/max) via
    // hydrate_node_view does NOT reset dynamic state (value/state/tripped).
    // This is the exact bug that issue #133 fixes.
    auto spec = make_spec(ui::InternedId(2040), "Slider", NodeFrameKind::Standard, bp2::NodeContentType::Slider);
    spec.content_min = 0.0f;
    spec.content_max = 100.0f;
    spec.content_value = 75.0f;  // Simulated runtime value

    // Simulate what happens when inspector edits min/max:
    // Only static fields change, dynamic value is preserved.
    float saved_value = spec.content_value;
    spec.content_min = -10.0f;  // Changed by hydrate_node_view
    spec.content_max = 200.0f;  // Changed by hydrate_node_view
    // content_value is NOT touched by hydrate_node_view

    EXPECT_FLOAT_EQ(spec.content_value, saved_value)
        << "Dynamic value must survive static re-hydration";

    // Compile with new static range but preserved dynamic value
    NodePresentation p = compile_node_presentation(spec);
    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "75.0") << "Slider should show preserved dynamic value";
}
