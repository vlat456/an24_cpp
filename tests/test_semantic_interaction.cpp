#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_interaction.h"

using namespace editor::presentation;

namespace {

SceneHitObject make_content_region() {
    SceneHitObject object;
    object.id = SceneObjectId(1);
    object.node_id = ui::InternedId(10);
    object.element_id = ui::InternedId(20);
    object.region_id = ui::InternedId(30);
    object.kind = SceneHitObjectKind::ContentRegion;
    object.shape = HitShapeKind::Rectangle;
    object.bounds = Rect{0.0f, 0.0f, 10.0f, 10.0f};
    return object;
}

InteractionBinding make_binding(InteractionKind kind, ui::InternedId action_id) {
    InteractionBinding binding;
    binding.region_id = ui::InternedId(30);
    binding.kind = kind;
    binding.action_id = action_id;
    binding.min_value = 1.0f;
    binding.max_value = 9.0f;
    binding.step = 2.0f;
    return binding;
}

} // namespace

TEST(SemanticInteractionTest, EmptyHitDoesNotResolveInteraction) {
    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitEmpty{}, PointerPhase::Press);

    EXPECT_FALSE(request.has_value());
}

TEST(SemanticInteractionTest, NodeBodyHitDoesNotResolveInteraction) {
    SceneHitObject object;
    object.kind = SceneHitObjectKind::NodeBody;

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitNodeBody{&object}, PointerPhase::Press);

    EXPECT_FALSE(request.has_value());
}

TEST(SemanticInteractionTest, PressPhaseResolvesClickBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::Click, ui::InternedId(100)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Press);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->node_id, ui::InternedId(10));
    EXPECT_EQ(request->element_id, ui::InternedId(20));
    EXPECT_EQ(request->region_id, ui::InternedId(30));
    EXPECT_EQ(request->action_id, ui::InternedId(100));
    EXPECT_EQ(request->kind, InteractionKind::Click);
}

TEST(SemanticInteractionTest, PressPhaseResolvesPressBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::Press, ui::InternedId(101)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Press);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->action_id, ui::InternedId(101));
    EXPECT_EQ(request->kind, InteractionKind::Press);
}

TEST(SemanticInteractionTest, DragPhaseResolvesDragScalarBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::DragScalar, ui::InternedId(102)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Drag);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->action_id, ui::InternedId(102));
    EXPECT_EQ(request->kind, InteractionKind::DragScalar);
    EXPECT_FLOAT_EQ(request->min_value, 1.0f);
    EXPECT_FLOAT_EQ(request->max_value, 9.0f);
    EXPECT_FLOAT_EQ(request->step, 2.0f);
}

TEST(SemanticInteractionTest, DragPhaseResolvesDragDiscreteBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::DragDiscrete, ui::InternedId(103)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Drag);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->action_id, ui::InternedId(103));
    EXPECT_EQ(request->kind, InteractionKind::DragDiscrete);
}

TEST(SemanticInteractionTest, ReleasePhaseResolvesReleaseBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::Release, ui::InternedId(104)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Release);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->action_id, ui::InternedId(104));
    EXPECT_EQ(request->kind, InteractionKind::Release);
}

TEST(SemanticInteractionTest, NonMatchingPhaseDoesNotResolveBinding) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::Release, ui::InternedId(105)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Press);

    EXPECT_FALSE(request.has_value());
}

TEST(SemanticInteractionTest, ReturnsFirstMatchingBindingInStoredOrder) {
    SceneHitObject object = make_content_region();
    object.interactions.push_back(make_binding(InteractionKind::DragScalar, ui::InternedId(106)));
    object.interactions.push_back(make_binding(InteractionKind::DragDiscrete, ui::InternedId(107)));

    std::optional<SemanticInteractionRequest> request =
        resolve_semantic_interaction(SemanticHitContentRegion{&object}, PointerPhase::Drag);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->action_id, ui::InternedId(106));
    EXPECT_EQ(request->kind, InteractionKind::DragScalar);
}
