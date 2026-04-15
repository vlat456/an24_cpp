#include <gtest/gtest.h>

#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"

using namespace editor::presentation;

namespace {

PresentationFragment make_test_fragment(const bp2::Blueprint::Node& /*node*/, ui::InternedId /*type_id*/) {
    PresentationFragment fragment;
    fragment.root.element_id = ui::InternedId(1);
    fragment.root.layout = LayoutKind::Column;
    fragment.root.gap = 4.0f;

    PresentationNode label;
    label.element_id = ui::InternedId(2);
    PaintCommand label_paint;
    label_paint.id = ui::InternedId(3);
    label_paint.kind = PaintPrimitiveKind::Text;
    label_paint.text = "TEST";
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
    control_binding.kind = InteractionKind::Click;
    control_binding.action_id = ui::InternedId(7);
    control.interactions.push_back(control_binding);

    fragment.root.children.push_back(std::move(label));
    fragment.root.children.push_back(std::move(control));
    return fragment;
}

NodePresentation make_test_presentation() {
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(100);
    node.view.name = "Test Node";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(200), NodePresenter{NodeFrameKind::Standard, &make_test_fragment});
    return compile_node_presentation(NodePresentationCompileContext{&registry}, node, ui::InternedId(200));
}

} // namespace

TEST(SemanticSceneHitTestTest, EmptySnapshotReturnsHitEmpty) {
    SemanticSceneSnapshot snapshot;
    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(10.0f, 10.0f));
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(result));
}

TEST(SemanticSceneHitTestTest, PointInsideNodeBodyReturnsNodeBody) {
    NodePresentation presentation = make_test_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    ASSERT_GT(snapshot.hit_objects.size(), 0u);

    const SceneHitObject* node_body = nullptr;
    for (const auto& obj : snapshot.hit_objects) {
        if (obj.kind == SceneHitObjectKind::NodeBody) {
            node_body = &obj;
            break;
        }
    }
    ASSERT_NE(node_body, nullptr);

    ui::Pt test_point(node_body->bounds.x + 10.0f, node_body->bounds.y + 10.0f);
    SemanticHitResult result = hit_test_semantic_scene(snapshot, test_point);

    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(result));
    auto hit = std::get<SemanticHitNodeBody>(result);
    EXPECT_EQ(hit.object->kind, SceneHitObjectKind::NodeBody);
}

TEST(SemanticSceneHitTestTest, PointInsideContentRegionReturnsContentRegion) {
    NodePresentation presentation = make_test_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));
    SemanticSceneSnapshot snapshot = build_semantic_scene_snapshot(presentation, layout);

    const SceneHitObject* content_region = nullptr;
    for (const auto& obj : snapshot.hit_objects) {
        if (obj.kind == SceneHitObjectKind::ContentRegion) {
            content_region = &obj;
            break;
        }
    }
    ASSERT_NE(content_region, nullptr);

    ui::Pt test_point(content_region->bounds.x + content_region->bounds.w * 0.5f,
                      content_region->bounds.y + content_region->bounds.h * 0.5f);
    SemanticHitResult result = hit_test_semantic_scene(snapshot, test_point);

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
    auto hit = std::get<SemanticHitContentRegion>(result);
    EXPECT_EQ(hit.object->kind, SceneHitObjectKind::ContentRegion);
}

TEST(SemanticSceneHitTestTest, ContentRegionWinsOverOverlappingNodeBody) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject node_body;
    node_body.id = SceneObjectId(1);
    node_body.node_id = ui::InternedId(10);
    node_body.kind = SceneHitObjectKind::NodeBody;
    node_body.shape = HitShapeKind::Rectangle;
    node_body.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(node_body);

    SceneHitObject content_region;
    content_region.id = SceneObjectId(2);
    content_region.node_id = ui::InternedId(10);
    content_region.element_id = ui::InternedId(20);
    content_region.region_id = ui::InternedId(30);
    content_region.kind = SceneHitObjectKind::ContentRegion;
    content_region.shape = HitShapeKind::Rectangle;
    content_region.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(content_region);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
}

TEST(SemanticSceneHitTestTest, LaterOverlappingContentRegionWinsOverEarlier) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject region1;
    region1.id = SceneObjectId(1);
    region1.node_id = ui::InternedId(10);
    region1.element_id = ui::InternedId(20);
    region1.region_id = ui::InternedId(30);
    region1.kind = SceneHitObjectKind::ContentRegion;
    region1.shape = HitShapeKind::Rectangle;
    region1.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(region1);

    SceneHitObject region2;
    region2.id = SceneObjectId(2);
    region2.node_id = ui::InternedId(10);
    region2.element_id = ui::InternedId(21);
    region2.region_id = ui::InternedId(31);
    region2.kind = SceneHitObjectKind::ContentRegion;
    region2.shape = HitShapeKind::Rectangle;
    region2.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(region2);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
    auto hit = std::get<SemanticHitContentRegion>(result);
    EXPECT_EQ(hit.object->id, region2.id);
}

TEST(SemanticSceneHitTestTest, RectangleHitIncludesEdges) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject rect;
    rect.id = SceneObjectId(1);
    rect.node_id = ui::InternedId(10);
    rect.kind = SceneHitObjectKind::NodeBody;
    rect.shape = HitShapeKind::Rectangle;
    rect.bounds = ui::Rect{10.0f, 20.0f, 30.0f, 40.0f};
    snapshot.hit_objects.push_back(rect);

    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(10.0f, 40.0f))));
    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(40.0f, 40.0f))));
    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(25.0f, 20.0f))));
    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(25.0f, 60.0f))));
}

TEST(SemanticSceneHitTestTest, CircleHitAcceptsPointAtCenterAndRejectsOutside) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject circle;
    circle.id = SceneObjectId(1);
    circle.node_id = ui::InternedId(10);
    circle.kind = SceneHitObjectKind::ContentRegion;
    circle.shape = HitShapeKind::Circle;
    circle.bounds = ui::Rect{0.0f, 0.0f, 40.0f, 40.0f};
    snapshot.hit_objects.push_back(circle);

    // Center
    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(
        hit_test_semantic_scene(snapshot, ui::Pt(20.0f, 20.0f))));
    // On boundary (distance == radius)
    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(
        hit_test_semantic_scene(snapshot, ui::Pt(40.0f, 20.0f))));
    // Outside inscribed circle but inside bounding box
    ASSERT_TRUE(std::holds_alternative<SemanticHitEmpty>(
        hit_test_semantic_scene(snapshot, ui::Pt(35.0f, 35.0f))));
}

TEST(SemanticSceneHitTestTest, LaterOverlappingNodeBodyWinsOverEarlier) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject body1;
    body1.id = SceneObjectId(1);
    body1.node_id = ui::InternedId(10);
    body1.kind = SceneHitObjectKind::NodeBody;
    body1.shape = HitShapeKind::Rectangle;
    body1.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(body1);

    SceneHitObject body2;
    body2.id = SceneObjectId(2);
    body2.node_id = ui::InternedId(11);
    body2.kind = SceneHitObjectKind::NodeBody;
    body2.shape = HitShapeKind::Rectangle;
    body2.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(body2);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(result));
    EXPECT_EQ(std::get<SemanticHitNodeBody>(result).object->id, body2.id);
}

TEST(SemanticSceneHitTestTest, PointOutsideAllObjectsReturnsEmpty) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject body;
    body.id = SceneObjectId(1);
    body.node_id = ui::InternedId(10);
    body.kind = SceneHitObjectKind::NodeBody;
    body.shape = HitShapeKind::Rectangle;
    body.bounds = ui::Rect{100.0f, 100.0f, 50.0f, 50.0f};
    snapshot.hit_objects.push_back(body);

    SceneHitObject region;
    region.id = SceneObjectId(2);
    region.node_id = ui::InternedId(10);
    region.kind = SceneHitObjectKind::ContentRegion;
    region.shape = HitShapeKind::Rectangle;
    region.bounds = ui::Rect{200.0f, 200.0f, 50.0f, 50.0f};
    snapshot.hit_objects.push_back(region);

    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(
        hit_test_semantic_scene(snapshot, ui::Pt(0.0f, 0.0f))));
}

TEST(SemanticSceneHitTestTest, CircleWithNonSquareBoundsUsesInscribedRadius) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject circle;
    circle.id = SceneObjectId(1);
    circle.node_id = ui::InternedId(10);
    circle.kind = SceneHitObjectKind::ContentRegion;
    circle.shape = HitShapeKind::Circle;
    circle.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 40.0f};
    snapshot.hit_objects.push_back(circle);

    // Center hit
    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(
        hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 20.0f))));
    // Outside inscribed circle (far right of wide rect)
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(
        hit_test_semantic_scene(snapshot, ui::Pt(85.0f, 20.0f))));
    // Just inside inscribed circle boundary
    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(
        hit_test_semantic_scene(snapshot, ui::Pt(69.0f, 20.0f))));
}

TEST(SemanticSceneHitTestTest, ZeroSizeRectangleHitsOnlyAtOrigin) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject zero_rect;
    zero_rect.id = SceneObjectId(1);
    zero_rect.node_id = ui::InternedId(10);
    zero_rect.kind = SceneHitObjectKind::NodeBody;
    zero_rect.shape = HitShapeKind::Rectangle;
    zero_rect.bounds = ui::Rect{50.0f, 50.0f, 0.0f, 0.0f};
    snapshot.hit_objects.push_back(zero_rect);

    EXPECT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f))));
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(
        hit_test_semantic_scene(snapshot, ui::Pt(50.1f, 50.0f))));
}

TEST(SemanticSceneHitTestTest, NegativeCoordinatesWorkCorrectly) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject body;
    body.id = SceneObjectId(1);
    body.node_id = ui::InternedId(10);
    body.kind = SceneHitObjectKind::NodeBody;
    body.shape = HitShapeKind::Rectangle;
    body.bounds = ui::Rect{-100.0f, -100.0f, 200.0f, 200.0f};
    snapshot.hit_objects.push_back(body);

    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(0.0f, 0.0f))));
    ASSERT_TRUE(std::holds_alternative<SemanticHitNodeBody>(
        hit_test_semantic_scene(snapshot, ui::Pt(-50.0f, -50.0f))));
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(
        hit_test_semantic_scene(snapshot, ui::Pt(-100.1f, 0.0f))));
}

TEST(SemanticSceneHitTestTest, ContentRegionWinsEvenWhenNodeBodyIsLaterInArray) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject region;
    region.id = SceneObjectId(1);
    region.node_id = ui::InternedId(10);
    region.element_id = ui::InternedId(20);
    region.region_id = ui::InternedId(30);
    region.kind = SceneHitObjectKind::ContentRegion;
    region.shape = HitShapeKind::Rectangle;
    region.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(region);

    SceneHitObject body;
    body.id = SceneObjectId(2);
    body.node_id = ui::InternedId(10);
    body.kind = SceneHitObjectKind::NodeBody;
    body.shape = HitShapeKind::Rectangle;
    body.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(body);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
    EXPECT_EQ(std::get<SemanticHitContentRegion>(result).object->id, region.id);
}

TEST(SemanticSceneHitTestTest, HitResultCarriesInteractionBindings) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject region;
    region.id = SceneObjectId(1);
    region.node_id = ui::InternedId(10);
    region.element_id = ui::InternedId(20);
    region.region_id = ui::InternedId(30);
    region.kind = SceneHitObjectKind::ContentRegion;
    region.shape = HitShapeKind::Rectangle;
    region.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};

    InteractionBinding binding;
    binding.region_id = ui::InternedId(30);
    binding.kind = InteractionKind::Click;
    binding.action_id = ui::InternedId(42);
    region.interactions.push_back(binding);
    snapshot.hit_objects.push_back(region);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
    const auto& hit = std::get<SemanticHitContentRegion>(result);
    ASSERT_EQ(hit.object->interactions.size(), 1u);
    EXPECT_EQ(hit.object->interactions[0].action_id, ui::InternedId(42));
}

TEST(SemanticSceneHitTestTest, HitLayerOrderingOverridesArrayOrder) {
    SemanticSceneSnapshot snapshot;

    SceneHitObject body;
    body.id = SceneObjectId(1);
    body.node_id = ui::InternedId(10);
    body.kind = SceneHitObjectKind::NodeBody;
    body.shape = HitShapeKind::Rectangle;
    body.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(body);

    SceneHitObject region;
    region.id = SceneObjectId(2);
    region.node_id = ui::InternedId(10);
    region.element_id = ui::InternedId(20);
    region.region_id = ui::InternedId(30);
    region.kind = SceneHitObjectKind::ContentRegion;
    region.shape = HitShapeKind::Rectangle;
    region.bounds = ui::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    snapshot.hit_objects.push_back(region);

    SemanticHitResult result = hit_test_semantic_scene(snapshot, ui::Pt(50.0f, 50.0f));

    ASSERT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result));
    EXPECT_EQ(std::get<SemanticHitContentRegion>(result).object->id, SceneObjectId(2));
}
