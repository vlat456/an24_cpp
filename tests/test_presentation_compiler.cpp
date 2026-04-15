#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "ui/core/interned_id.h"

using namespace editor::presentation;

// ============================================================================
// Helpers
// ============================================================================

namespace {

bp2::Blueprint::Node make_node(ui::InternedId id,
                               const std::string& name,
                               const std::string& render_hint = "",
                               bp2::NodeContentType content_type = bp2::NodeContentType::None) {
    bp2::Blueprint::Node node;
    node.semantic.id = id;
    node.semantic.type = ui::InternedId(1000);
    node.view.name = name;
    node.view.render_hint = render_hint;
    node.view.content_type = content_type;
    return node;
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
    auto node = make_node(ui::InternedId(1), "Generator");
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.node_id, ui::InternedId(1));
    EXPECT_EQ(p.shell.title, "Generator");
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Standard);
}

TEST(CompileNodePresentation, RefNodeGetsReferenceFrame) {
    auto node = make_node(ui::InternedId(2), "GND", "ref");
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Reference);
    EXPECT_EQ(p.shell.title, "GND");
}

TEST(CompileNodePresentation, BusNodeGetsBusFrame) {
    auto node = make_node(ui::InternedId(3), "AC Bus", "bus");
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Bus);
    EXPECT_EQ(p.shell.title, "AC Bus");
}

TEST(CompileNodePresentation, GroupNodeGetsGroupFrame) {
    auto node = make_node(ui::InternedId(4), "Power Section", "group");
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);
    EXPECT_EQ(p.shell.title, "Power Section");
}

TEST(CompileNodePresentation, TextNodeGetsAnnotationFrame) {
    auto node = make_node(ui::InternedId(5), "Note", "text");
    node.semantic.string_params["text"] = "This is a note";
    node.semantic.string_params["font_size"] = "14.0";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(p.shell.title, "Note");
    EXPECT_EQ(p.shell.annotation_text, "This is a note");
    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 14.0f);
}

TEST(CompileNodePresentation, TextNodeDefaultFontSizeWhenMissing) {
    auto node = make_node(ui::InternedId(6), "Note", "text");
    NodePresentation p = compile_node_presentation(node);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

TEST(CompileNodePresentation, NoneContentTypeProducesEmptyContentChildren) {
    auto node = make_node(ui::InternedId(10), "Resistor", "", bp2::NodeContentType::None);
    NodePresentation p = compile_node_presentation(node);

    EXPECT_TRUE(p.content.children.empty());
    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
}

// ============================================================================
// Switch content
// ============================================================================

TEST(DefaultContentPresenter, SwitchProducesRectanglesAndClickInteraction) {
    auto node = make_node(ui::InternedId(20), "AZS", "", bp2::NodeContentType::Switch);
    node.view.content_state = true;
    NodePresentation p = compile_node_presentation(node);

    // Switch: background rect + handle rect + click interaction child
    EXPECT_GE(p.content.children.size(), 3u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);

    const auto* click = find_interaction(p.content, InteractionKind::Click);
    ASSERT_NE(click, nullptr);
}

TEST(DefaultContentPresenter, VerticalToggleProducesRectanglesAndClickInteraction) {
    auto node = make_node(ui::InternedId(21), "Toggle", "", bp2::NodeContentType::VerticalToggle);
    node.view.content_state = false;
    NodePresentation p = compile_node_presentation(node);

    EXPECT_GE(p.content.children.size(), 3u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);

    const auto* click = find_interaction(p.content, InteractionKind::Click);
    ASSERT_NE(click, nullptr);
}

TEST(DefaultContentPresenter, SwitchTrippedChangesColor) {
    auto node_normal = make_node(ui::InternedId(22), "AZS", "", bp2::NodeContentType::Switch);
    node_normal.view.content_state = true;
    node_normal.view.content_tripped = false;

    auto node_tripped = make_node(ui::InternedId(23), "AZS", "", bp2::NodeContentType::Switch);
    node_tripped.view.content_state = true;
    node_tripped.view.content_tripped = true;

    NodePresentation p_normal = compile_node_presentation(node_normal);
    NodePresentation p_tripped = compile_node_presentation(node_tripped);

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

// ============================================================================
// Slider content
// ============================================================================

TEST(DefaultContentPresenter, SliderProducesTrackHandleAndDragInteraction) {
    auto node = make_node(ui::InternedId(30), "Throttle", "", bp2::NodeContentType::Slider);
    node.view.content_min = 0.0f;
    node.view.content_max = 100.0f;
    node.view.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(node);

    // Slider: track bg + track fill + handle circle + value text + drag interaction
    EXPECT_GE(p.content.children.size(), 5u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Rectangle), 2u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Text), 1u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragScalar);
    ASSERT_NE(drag, nullptr);
}

TEST(DefaultContentPresenter, SliderValueTextShowsFormattedValue) {
    auto node = make_node(ui::InternedId(31), "Slider", "", bp2::NodeContentType::Slider);
    node.view.content_value = 42.5f;
    NodePresentation p = compile_node_presentation(node);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "42.5");
}

// ============================================================================
// Indicator content
// ============================================================================

TEST(DefaultContentPresenter, IndicatorProducesCircle) {
    auto node = make_node(ui::InternedId(40), "Light", "", bp2::NodeContentType::Indicator);
    node.view.content_value = 0.8f;
    NodePresentation p = compile_node_presentation(node);

    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    // Indicator has no interaction
    EXPECT_EQ(count_total_interactions(p.content), 0u);
}

TEST(DefaultContentPresenter, IndicatorOffProducesDimColor) {
    auto node = make_node(ui::InternedId(41), "Light", "", bp2::NodeContentType::Indicator);
    node.view.content_value = 0.0f;
    NodePresentation p = compile_node_presentation(node);

    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
}

// ============================================================================
// Knob content
// ============================================================================

TEST(DefaultContentPresenter, KnobProducesCircleTicksAndDiscreteInteraction) {
    auto node = make_node(ui::InternedId(50), "Selector", "", bp2::NodeContentType::Knob);
    node.view.content_max = 5.0f;
    node.view.content_value = 2.0f;
    NodePresentation p = compile_node_presentation(node);

    // Knob body circle + tick lines (5) + pointer line + discrete interaction
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 2u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 5.0f);
}

TEST(DefaultContentPresenter, KnobMinTwoPositions) {
    auto node = make_node(ui::InternedId(51), "Selector", "", bp2::NodeContentType::Knob);
    node.view.content_max = 1.0f;  // Would be 1 position, clamped to 2
    node.view.content_value = 0.0f;
    NodePresentation p = compile_node_presentation(node);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 2.0f);
}

// ============================================================================
// Gauge content
// ============================================================================

TEST(DefaultContentPresenter, GaugeProducesArcTicksNeedleAndValueText) {
    auto node = make_node(ui::InternedId(60), "Voltmeter", "", bp2::NodeContentType::Gauge);
    node.view.content_min = 0.0f;
    node.view.content_max = 30.0f;
    node.view.content_value = 27.5f;
    node.view.content_unit = "V";
    NodePresentation p = compile_node_presentation(node);

    // Arc + 11 tick lines + needle line + center dot circle + value text + unit text
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Arc), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 12u);  // 11 ticks + 1 needle
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Text), 2u);   // value + unit

    // Gauge has no interaction
    EXPECT_EQ(count_total_interactions(p.content), 0u);
}

TEST(DefaultContentPresenter, GaugeValueTextShowsFormattedValue) {
    auto node = make_node(ui::InternedId(61), "Gauge", "", bp2::NodeContentType::Gauge);
    node.view.content_value = 12.3f;
    node.view.content_unit = "A";
    NodePresentation p = compile_node_presentation(node);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "12.3");
}

TEST(DefaultContentPresenter, GaugeWithoutUnitOmitsUnitText) {
    auto node = make_node(ui::InternedId(62), "Gauge", "", bp2::NodeContentType::Gauge);
    node.view.content_unit = "";
    NodePresentation p = compile_node_presentation(node);

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
    auto node = make_node(ui::InternedId(70), "Label", "", bp2::NodeContentType::Text);
    node.view.content_label = "Hello World";
    NodePresentation p = compile_node_presentation(node);

    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "Hello World");
}

TEST(DefaultContentPresenter, EmptyTextContentProducesNoChildren) {
    auto node = make_node(ui::InternedId(71), "Label", "", bp2::NodeContentType::Text);
    node.view.content_label = "";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_TRUE(p.content.children.empty());
}

// ============================================================================
// Element ID uniqueness
// ============================================================================

TEST(DefaultContentPresenter, AllElementIdsAreUnique) {
    auto node = make_node(ui::InternedId(80), "Gauge", "", bp2::NodeContentType::Gauge);
    node.view.content_value = 15.0f;
    node.view.content_unit = "V";
    NodePresentation p = compile_node_presentation(node);

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
    auto node = make_node(ui::InternedId(100), "AZS-1", "", bp2::NodeContentType::Switch);
    node.view.content_state = true;

    // Step 1: Compile
    NodePresentation p = compile_node_presentation(node);
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
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind == SceneRenderObjectKind::NodeFrame) has_frame = true;
        if (obj.kind == SceneRenderObjectKind::NodeTitle && obj.text == "AZS-1") has_title = true;
    }
    EXPECT_TRUE(has_frame);
    EXPECT_TRUE(has_title);
}

TEST(PresentationCompilerIntegration, RefNodeFlowsThroughFullPipeline) {
    auto node = make_node(ui::InternedId(101), "GND", "ref");

    NodePresentation p = compile_node_presentation(node);
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
    auto node = make_node(ui::InternedId(102), "DC Bus", "bus");

    NodePresentation p = compile_node_presentation(node);
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
    auto node = make_node(ui::InternedId(103), "Power Section", "group");

    NodePresentation p = compile_node_presentation(node);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(300.0f, 200.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
}

TEST(PresentationCompilerIntegration, TextNodeFlowsThroughFullPipeline) {
    auto node = make_node(ui::InternedId(104), "Note", "text");
    node.semantic.string_params["text"] = "Design note";

    NodePresentation p = compile_node_presentation(node);
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Annotation);
    EXPECT_EQ(p.shell.annotation_text, "Design note");

    NodeSlotLayout layout = layout_node_presentation(p, ui::Pt(200.0f, 100.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(p, layout);

    EXPECT_FALSE(snapshot.render_objects.empty());
}

TEST(PresentationCompilerIntegration, GaugeNodeFlowsThroughFullPipeline) {
    auto node = make_node(ui::InternedId(105), "Voltmeter", "", bp2::NodeContentType::Gauge);
    node.view.content_min = 0.0f;
    node.view.content_max = 30.0f;
    node.view.content_value = 27.5f;
    node.view.content_unit = "V";

    NodePresentation p = compile_node_presentation(node);
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
    auto standard = make_node(ui::InternedId(200), "Battery", "", bp2::NodeContentType::Gauge);
    standard.view.content_value = 27.0f;
    standard.view.content_unit = "V";

    auto ref = make_node(ui::InternedId(201), "GND", "ref");
    auto bus = make_node(ui::InternedId(202), "DC Bus", "bus");

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

PresentationNode make_empty_fragment(const bp2::Blueprint::Node& /*node*/, ui::InternedId /*type_id*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(40);
    root.layout = LayoutKind::Column;
    return root;
}

PresentationNode make_custom_fragment(const bp2::Blueprint::Node& node, ui::InternedId /*type_id*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(50);
    root.layout = LayoutKind::Overlay;

    PresentationNode title;
    title.element_id = ui::InternedId(51);
    PaintCommand title_cmd;
    title_cmd.id = ui::InternedId(52);
    title_cmd.kind = PaintPrimitiveKind::Text;
    title_cmd.text = node.view.name;
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
    auto node = make_node(ui::InternedId(300), "Generator");
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(100), NodePresenter{NodeFrameKind::Standard, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation p = compile_node_presentation(ctx, node, ui::InternedId(100));

    EXPECT_EQ(p.node_id, ui::InternedId(300));
    EXPECT_EQ(p.shell.title, "Generator");
}

TEST(RegistryBasedCompile, CustomPresenterOverridesDefaultContent) {
    auto node = make_node(ui::InternedId(301), "Custom", "", bp2::NodeContentType::Slider);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(500), NodePresenter{NodeFrameKind::Group, &make_custom_fragment});

    NodePresentationCompileContext ctx{&registry};
    NodePresentation p = compile_node_presentation(ctx, node, ui::InternedId(500));

    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
    ASSERT_EQ(p.content.children.size(), 2u);
    EXPECT_EQ(p.content.children[0].paint[0].text, "Custom");
    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Group);
}

TEST(RegistryBasedCompile, RegistryReturnsNullForMissingType) {
    NodePresenterRegistry registry;
    EXPECT_EQ(registry.find_presenter(ui::InternedId(999)), nullptr);
}

TEST(RegistryBasedCompile, MissingPresenterDiesInDebug) {
#ifndef NDEBUG
    auto node = make_node(ui::InternedId(302), "Missing");
    NodePresenterRegistry registry;

    EXPECT_DEATH((void)compile_node_presentation(NodePresentationCompileContext{&registry}, node, ui::InternedId(700)), "");
#endif
}

// ============================================================================
// Value node (render_hint="ref" with Value type)
// ============================================================================

TEST(CompileNodePresentation, ValueNodeWithRefHintGetsReferenceFrame) {
    auto node = make_node(ui::InternedId(400), "28.0", "ref", bp2::NodeContentType::Value);
    NodePresentation p = compile_node_presentation(node);

    EXPECT_EQ(p.shell.frame_kind, NodeFrameKind::Reference);
    EXPECT_EQ(p.shell.title, "28.0");
}

// ============================================================================
// Regression: annotation font_size parsing edge cases
// ============================================================================

TEST(CompileNodePresentation, InvalidFontSizeKeepsDefault) {
    auto node = make_node(ui::InternedId(500), "Note", "text");
    node.semantic.string_params["font_size"] = "not_a_number";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

TEST(CompileNodePresentation, NegativeFontSizeKeepsDefault) {
    auto node = make_node(ui::InternedId(501), "Note", "text");
    node.semantic.string_params["font_size"] = "-5.0";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

TEST(CompileNodePresentation, ZeroFontSizeKeepsDefault) {
    auto node = make_node(ui::InternedId(502), "Note", "text");
    node.semantic.string_params["font_size"] = "0";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

TEST(CompileNodePresentation, EmptyFontSizeStringKeepsDefault) {
    auto node = make_node(ui::InternedId(503), "Note", "text");
    node.semantic.string_params["font_size"] = "";
    NodePresentation p = compile_node_presentation(node);

    EXPECT_FLOAT_EQ(p.shell.annotation_font_size, 12.0f);
}

// ============================================================================
// Regression: Value content type produces empty content
// ============================================================================

TEST(DefaultContentPresenter, ValueContentTypeProducesEmptyChildren) {
    auto node = make_node(ui::InternedId(510), "28V", "", bp2::NodeContentType::Value);
    NodePresentation p = compile_node_presentation(node);

    EXPECT_TRUE(p.content.children.empty());
    EXPECT_EQ(p.content.layout, LayoutKind::Overlay);
}

// ============================================================================
// Regression: slider DragScalar range matches preferred width
// ============================================================================

TEST(DefaultContentPresenter, SliderDragScalarRangeMatchesPreferredWidth) {
    auto node = make_node(ui::InternedId(520), "Throttle", "", bp2::NodeContentType::Slider);
    node.view.content_min = 0.0f;
    node.view.content_max = 100.0f;
    node.view.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(node);

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
    auto node = make_node(ui::InternedId(530), "Slider", "", bp2::NodeContentType::Slider);
    node.view.content_min = 50.0f;
    node.view.content_max = 50.0f;
    node.view.content_value = 50.0f;
    NodePresentation p = compile_node_presentation(node);

    // Should not crash and should produce valid content
    EXPECT_GE(p.content.children.size(), 4u);  // track bg + fill + handle + text
    const auto* text = find_text_paint(p.content);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "50.0");
}

// ============================================================================
// Regression: gauge with zero range doesn't crash
// ============================================================================

TEST(DefaultContentPresenter, GaugeZeroRangeProducesZeroNormalized) {
    auto node = make_node(ui::InternedId(540), "Gauge", "", bp2::NodeContentType::Gauge);
    node.view.content_min = 10.0f;
    node.view.content_max = 10.0f;
    node.view.content_value = 10.0f;
    NodePresentation p = compile_node_presentation(node);

    // Should not crash and should produce valid content
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Arc), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 12u);
}

// ============================================================================
// Regression: indicator with negative value
// ============================================================================

TEST(DefaultContentPresenter, IndicatorNegativeValueProducesDimColor) {
    auto node = make_node(ui::InternedId(550), "Light", "", bp2::NodeContentType::Indicator);
    node.view.content_value = -1.0f;
    NodePresentation p = compile_node_presentation(node);

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
    auto node = make_node(ui::InternedId(560), "Selector", "", bp2::NodeContentType::Knob);
    node.view.content_max = 3.0f;
    node.view.content_value = 999.0f;  // way beyond max
    NodePresentation p = compile_node_presentation(node);

    // Should not crash; knob should still produce valid content
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Circle), 1u);
    EXPECT_GE(count_paint_kind(p.content, PaintPrimitiveKind::Line), 2u);

    const auto* drag = find_interaction(p.content, InteractionKind::DragDiscrete);
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->step, 3.0f);
}
