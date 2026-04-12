#include <gtest/gtest.h>

#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"

using namespace editor::presentation;

namespace {

PresentationFragment make_snapshot_fragment(const bp2::Blueprint::Node& /*node*/, ui::InternedId /*type_id*/) {
    PresentationFragment fragment;
    fragment.root.element_id = ui::InternedId(1);
    fragment.root.layout = LayoutKind::Column;
    fragment.root.gap = 4.0f;

    PresentationNode label;
    label.element_id = ui::InternedId(2);
    PaintCommand label_paint;
    label_paint.id = ui::InternedId(3);
    label_paint.kind = PaintPrimitiveKind::Text;
    label_paint.text = "VOLTS";
    label.paint.push_back(std::move(label_paint));

    PresentationNode control;
    control.element_id = ui::InternedId(4);
    PaintCommand control_paint;
    control_paint.id = ui::InternedId(5);
    control_paint.kind = PaintPrimitiveKind::Circle;
    control.paint.push_back(std::move(control_paint));

    HitRegion control_region;
    control_region.id = ui::InternedId(6);
    control_region.kind = HitShapeKind::Circle;
    control.hit_regions.push_back(control_region);

    InteractionBinding control_binding;
    control_binding.region_id = ui::InternedId(6);
    control_binding.kind = InteractionKind::DragDiscrete;
    control_binding.action_id = ui::InternedId(7);
    control_binding.min_value = 0.0f;
    control_binding.max_value = 3.0f;
    control_binding.step = 1.0f;
    control.interactions.push_back(control_binding);

    fragment.root.children.push_back(std::move(label));
    fragment.root.children.push_back(std::move(control));
    return fragment;
}

NodePresentation make_presentation() {
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(100);
    node.view.name = "AC Bus";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(200), NodePresenter{NodeFrameKind::Bus, &make_snapshot_fragment});
    return compile_node_presentation(NodePresentationCompileContext{&registry}, node, ui::InternedId(200));
}

const SceneRenderObject* find_render(const SemanticSceneSnapshot& snapshot,
                                     SceneRenderObjectKind kind,
                                     ui::InternedId element_id = ui::InternedId()) {
    for (const SceneRenderObject& object : snapshot.render_objects) {
        if (object.kind == kind && (element_id.empty() || object.element_id == element_id)) {
            return &object;
        }
    }
    return nullptr;
}

const SceneHitObject* find_hit(const SemanticSceneSnapshot& snapshot,
                               SceneHitObjectKind kind,
                               ui::InternedId region_id = ui::InternedId()) {
    for (const SceneHitObject& object : snapshot.hit_objects) {
        if (object.kind == kind && (region_id.empty() || object.region_id == region_id)) {
            return &object;
        }
    }
    return nullptr;
}

} // namespace

TEST(SemanticSceneSnapshotTest, BuildsNodeFrameAndTitleObjects) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneRenderObject* frame = find_render(snapshot, SceneRenderObjectKind::NodeFrame);
    const SceneRenderObject* title = find_render(snapshot, SceneRenderObjectKind::NodeTitle);
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(title, nullptr);

    EXPECT_EQ(frame->frame_kind, NodeFrameKind::Bus);
    EXPECT_EQ(frame->primitive, PaintPrimitiveKind::Rectangle);
    EXPECT_EQ(title->text, "AC Bus");
    EXPECT_EQ(title->primitive, PaintPrimitiveKind::Text);
}

TEST(SemanticSceneSnapshotTest, BuildsNodeBodyHitObjectFromBodySlot) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneHitObject* body = find_hit(snapshot, SceneHitObjectKind::NodeBody);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->shape, HitShapeKind::Rectangle);
    EXPECT_FLOAT_EQ(body->bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(body->bounds.w, 140.0f);
}

TEST(SemanticSceneSnapshotTest, BuildsContentRenderObjectsFromFragmentPaint) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneRenderObject* label = find_render(snapshot, SceneRenderObjectKind::ContentPaint, ui::InternedId(2));
    const SceneRenderObject* control = find_render(snapshot, SceneRenderObjectKind::ContentPaint, ui::InternedId(4));
    ASSERT_NE(label, nullptr);
    ASSERT_NE(control, nullptr);

    EXPECT_EQ(label->primitive, PaintPrimitiveKind::Text);
    EXPECT_EQ(label->text, "VOLTS");
    EXPECT_EQ(control->primitive, PaintPrimitiveKind::Circle);
}

TEST(SemanticSceneSnapshotTest, BuildsContentHitObjectsWithInteractionBindings) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneHitObject* control = find_hit(snapshot, SceneHitObjectKind::ContentRegion, ui::InternedId(6));
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(control->shape, HitShapeKind::Circle);
    ASSERT_EQ(control->interactions.size(), 1u);
    EXPECT_EQ(control->interactions[0].kind, InteractionKind::DragDiscrete);
    EXPECT_FLOAT_EQ(control->interactions[0].step, 1.0f);
}

TEST(SemanticSceneSnapshotTest, UsesElementPlacementsForContentObjectBounds) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneRenderObject* label = find_render(snapshot, SceneRenderObjectKind::ContentPaint, ui::InternedId(2));
    const SceneHitObject* control = find_hit(snapshot, SceneHitObjectKind::ContentRegion, ui::InternedId(6));
    ASSERT_NE(label, nullptr);
    ASSERT_NE(control, nullptr);

    EXPECT_FLOAT_EQ(label->bounds.x, 28.0f);
    EXPECT_FLOAT_EQ(label->bounds.h, 38.0f);
    EXPECT_FLOAT_EQ(control->bounds.y, 74.0f);
    EXPECT_FLOAT_EQ(control->bounds.h, 38.0f);
}

TEST(SemanticSceneSnapshotTest, MultiNodeBuilderAssignsUniqueObjectIdsAcrossNodes) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    ASSERT_EQ(snapshot.render_objects.size(), 8u);
    ASSERT_EQ(snapshot.hit_objects.size(), 4u);
    ASSERT_EQ(snapshot.node_index.size(), 2u);

    for (size_t i = 0; i + 1 < snapshot.render_objects.size(); ++i) {
        for (size_t j = i + 1; j < snapshot.render_objects.size(); ++j) {
            EXPECT_NE(snapshot.render_objects[i].id, snapshot.render_objects[j].id);
        }
    }
    for (size_t i = 0; i + 1 < snapshot.hit_objects.size(); ++i) {
        for (size_t j = i + 1; j < snapshot.hit_objects.size(); ++j) {
            EXPECT_NE(snapshot.hit_objects[i].id, snapshot.hit_objects[j].id);
        }
    }
}

TEST(SemanticSceneSnapshotTest, NodeIndexTracksPerNodeObjectRanges) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    const SceneNodeIndexEntry* first_index = find_scene_node_index(snapshot, ui::InternedId(100));
    const SceneNodeIndexEntry* second_index = find_scene_node_index(snapshot, ui::InternedId(101));
    ASSERT_NE(first_index, nullptr);
    ASSERT_NE(second_index, nullptr);

    EXPECT_EQ(first_index->render_range.offset, 0u);
    EXPECT_EQ(first_index->render_range.count, 4u);
    EXPECT_EQ(first_index->hit_range.offset, 0u);
    EXPECT_EQ(first_index->hit_range.count, 2u);

    EXPECT_EQ(second_index->render_range.offset, 4u);
    EXPECT_EQ(second_index->render_range.count, 4u);
    EXPECT_EQ(second_index->hit_range.offset, 2u);
    EXPECT_EQ(second_index->hit_range.count, 2u);
}

TEST(SemanticSceneSnapshotTest, NodeIndexSupportsNodeLocalRenderRangeIteration) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    const SceneNodeIndexEntry* second_index = find_scene_node_index(snapshot, ui::InternedId(101));
    ASSERT_NE(second_index, nullptr);

    bool saw_only_second_node = true;
    for (size_t i = 0; i < second_index->render_range.count; ++i) {
        const SceneRenderObject& object =
            snapshot.render_objects[second_index->render_range.offset + i];
        if (object.node_id != ui::InternedId(101)) {
            saw_only_second_node = false;
        }
    }

    EXPECT_TRUE(saw_only_second_node);
}

TEST(SemanticSceneSnapshotTest, MultiNodeBuilderPreservesPerNodeTitles) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    bool saw_ac = false;
    bool saw_dc = false;
    for (const SceneRenderObject& object : snapshot.render_objects) {
        if (object.kind != SceneRenderObjectKind::NodeTitle) {
            continue;
        }
        if (object.text == "AC Bus") {
            saw_ac = true;
        }
        if (object.text == "DC Bus") {
            saw_dc = true;
        }
    }

    EXPECT_TRUE(saw_ac);
    EXPECT_TRUE(saw_dc);
}

TEST(SemanticSceneSnapshotTest, FindRenderObjectByIdReturnsMatchingObject) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    ASSERT_FALSE(snapshot.render_objects.empty());
    SceneObjectId id = snapshot.render_objects[0].id;

    const SceneRenderObject* object = find_render_object_by_id(snapshot, id);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->id, id);
}

TEST(SemanticSceneSnapshotTest, FindHitObjectByIdReturnsMatchingObject) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    ASSERT_FALSE(snapshot.hit_objects.empty());
    SceneObjectId id = snapshot.hit_objects[0].id;

    const SceneHitObject* object = find_hit_object_by_id(snapshot, id);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->id, id);
}

TEST(SemanticSceneSnapshotTest, RenderLayerOrderIsConsistentWithKind) {
    EXPECT_LT(render_layer_order(SceneRenderObjectKind::NodeFrame),
              render_layer_order(SceneRenderObjectKind::NodeTitle));
    EXPECT_LT(render_layer_order(SceneRenderObjectKind::NodeTitle),
              render_layer_order(SceneRenderObjectKind::ContentPaint));
}

TEST(SemanticSceneSnapshotTest, OrderedRenderObjectsSortByKindLayerAndPreserveStability) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    std::vector<const SceneRenderObject*> ordered = ordered_render_objects(snapshot);
    ASSERT_EQ(ordered.size(), snapshot.render_objects.size());

    ASSERT_GE(ordered.size(), 8u);
    EXPECT_EQ(ordered[0]->kind, SceneRenderObjectKind::NodeFrame);
    EXPECT_EQ(ordered[1]->kind, SceneRenderObjectKind::NodeFrame);
    EXPECT_EQ(ordered[2]->kind, SceneRenderObjectKind::NodeTitle);
    EXPECT_EQ(ordered[3]->kind, SceneRenderObjectKind::NodeTitle);
    EXPECT_EQ(ordered[4]->kind, SceneRenderObjectKind::ContentPaint);
    EXPECT_EQ(ordered[5]->kind, SceneRenderObjectKind::ContentPaint);
    EXPECT_EQ(ordered[6]->kind, SceneRenderObjectKind::ContentPaint);
    EXPECT_EQ(ordered[7]->kind, SceneRenderObjectKind::ContentPaint);

    // Stable ordering within a layer should preserve original scene insertion order.
    EXPECT_EQ(ordered[0]->node_id, ui::InternedId(100));
    EXPECT_EQ(ordered[1]->node_id, ui::InternedId(101));
    EXPECT_EQ(ordered[2]->node_id, ui::InternedId(100));
    EXPECT_EQ(ordered[3]->node_id, ui::InternedId(101));
}

TEST(SemanticSceneSnapshotTest, HitLayerOrderIsConsistentWithKind) {
    EXPECT_LT(hit_layer_order(SceneHitObjectKind::NodeBody),
              hit_layer_order(SceneHitObjectKind::ContentRegion));
}

TEST(SemanticSceneSnapshotTest, OrderedHitObjectsSortByKindLayerAndPreserveStability) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject content_a;
    content_a.id = SceneObjectId(1);
    content_a.node_id = ui::InternedId(100);
    content_a.kind = SceneHitObjectKind::ContentRegion;
    snapshot.hit_objects.push_back(content_a);

    SceneHitObject body_a;
    body_a.id = SceneObjectId(2);
    body_a.node_id = ui::InternedId(100);
    body_a.kind = SceneHitObjectKind::NodeBody;
    snapshot.hit_objects.push_back(body_a);

    SceneHitObject content_b;
    content_b.id = SceneObjectId(3);
    content_b.node_id = ui::InternedId(101);
    content_b.kind = SceneHitObjectKind::ContentRegion;
    snapshot.hit_objects.push_back(content_b);

    std::vector<const SceneHitObject*> ordered = ordered_hit_objects(snapshot);
    ASSERT_EQ(ordered.size(), 3u);

    EXPECT_EQ(ordered[0]->kind, SceneHitObjectKind::ContentRegion);
    EXPECT_EQ(ordered[1]->kind, SceneHitObjectKind::ContentRegion);
    EXPECT_EQ(ordered[2]->kind, SceneHitObjectKind::NodeBody);

    // Higher layer first, later object first within the same layer.
    EXPECT_EQ(ordered[0]->id, SceneObjectId(3));
    EXPECT_EQ(ordered[1]->id, SceneObjectId(1));
}

TEST(SemanticSceneSnapshotTest, FindHitObjectByRegionIdReturnsMatchingObject) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneHitObject* object = find_hit_object_by_region_id(snapshot, ui::InternedId(6));
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->region_id, ui::InternedId(6));
    EXPECT_EQ(object->kind, SceneHitObjectKind::ContentRegion);
}

TEST(SemanticSceneSnapshotTest, HitObjectsForNodeReturnsOnlyNodeLocalHits) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    std::vector<const SceneHitObject*> hits = hit_objects_for_node(snapshot, ui::InternedId(101));
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0]->node_id, ui::InternedId(101));
    EXPECT_EQ(hits[1]->node_id, ui::InternedId(101));
}

TEST(SemanticSceneSnapshotTest, HitObjectsForRegionReturnsMatchingRegionHitsOnly) {
    NodePresentation first = make_presentation();
    NodeSlotLayout first_layout = layout_node_presentation(first, ui::Pt(180.0f, 120.0f));

    NodePresentation second = make_presentation();
    second.node_id = ui::InternedId(101);
    second.shell.title = "DC Bus";
    NodeSlotLayout second_layout = layout_node_presentation(second, ui::Pt(200.0f, 140.0f));

    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot({
        SemanticSceneNode{first, first_layout},
        SemanticSceneNode{second, second_layout},
    });

    std::vector<const SceneHitObject*> hits = hit_objects_for_region(snapshot, ui::InternedId(6));
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0]->region_id, ui::InternedId(6));
    EXPECT_EQ(hits[1]->region_id, ui::InternedId(6));
    EXPECT_EQ(hits[0]->kind, SceneHitObjectKind::ContentRegion);
    EXPECT_EQ(hits[1]->kind, SceneHitObjectKind::ContentRegion);
}

TEST(SemanticSceneSnapshotTest, FindRenderObjectByIdReturnsNullptrOnMiss) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_EQ(find_render_object_by_id(snapshot, SceneObjectId(9999)), nullptr);
}

TEST(SemanticSceneSnapshotTest, FindHitObjectByIdReturnsNullptrOnMiss) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_EQ(find_hit_object_by_id(snapshot, SceneObjectId(9999)), nullptr);
}

TEST(SemanticSceneSnapshotTest, FindSceneNodeIndexReturnsNullptrOnMiss) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_EQ(find_scene_node_index(snapshot, ui::InternedId(9999)), nullptr);
}

TEST(SemanticSceneSnapshotTest, FindHitObjectByRegionIdReturnsNullptrOnMiss) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_EQ(find_hit_object_by_region_id(snapshot, ui::InternedId(9999)), nullptr);
}

TEST(SemanticSceneSnapshotTest, OrderedViewsAreEmptyForEmptySnapshot) {
    SemanticSceneSnapshot snapshot;

    EXPECT_TRUE(ordered_render_objects(snapshot).empty());
    EXPECT_TRUE(ordered_hit_objects(snapshot).empty());
}

TEST(SemanticSceneSnapshotTest, HitObjectsForNodeReturnsEmptyForMissingNode) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_TRUE(hit_objects_for_node(snapshot, ui::InternedId(9999)).empty());
}

TEST(SemanticSceneSnapshotTest, HitObjectsForRegionReturnsEmptyForMissingRegion) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    EXPECT_TRUE(hit_objects_for_region(snapshot, ui::InternedId(9999)).empty());
}
