#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_interaction_session.h"

using namespace editor::presentation;

namespace {

SemanticInteractionRequest make_request(ui::InternedId node_id, ui::InternedId element_id,
                                        ui::InternedId region_id, ui::InternedId action_id,
                                        InteractionKind kind) {
    SemanticInteractionRequest request;
    request.node_id = node_id;
    request.element_id = element_id;
    request.region_id = region_id;
    request.action_id = action_id;
    request.kind = kind;
    return request;
}

SceneHitObject make_content_region(ui::InternedId node_id, ui::InternedId element_id,
                                   ui::InternedId region_id) {
    SceneHitObject object;
    object.node_id = node_id;
    object.element_id = element_id;
    object.region_id = region_id;
    object.kind = SceneHitObjectKind::ContentRegion;
    return object;
}

SceneHitObject make_node_body(ui::InternedId node_id) {
    SceneHitObject object;
    object.node_id = node_id;
    object.kind = SceneHitObjectKind::NodeBody;
    return object;
}

} // namespace

TEST(SemanticInteractionSessionTest, BeginCreatesActiveSessionAndCopiesIdentifiers) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Click);

    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    EXPECT_TRUE(session.active);
    EXPECT_EQ(session.node_id, ui::InternedId(10));
    EXPECT_EQ(session.element_id, ui::InternedId(20));
    EXPECT_EQ(session.region_id, ui::InternedId(30));
    EXPECT_EQ(session.action_id, ui::InternedId(100));
    EXPECT_EQ(session.kind, InteractionKind::Click);
}

TEST(SemanticInteractionSessionTest, EndReturnsInactiveEmptySession) {
    SemanticInteractionSession session = end_semantic_interaction_session();

    EXPECT_FALSE(session.active);
    EXPECT_EQ(session.node_id, ui::InternedId(0));
    EXPECT_EQ(session.element_id, ui::InternedId(0));
    EXPECT_EQ(session.region_id, ui::InternedId(0));
    EXPECT_EQ(session.action_id, ui::InternedId(0));
    EXPECT_EQ(session.kind, InteractionKind::Click);
    EXPECT_FLOAT_EQ(session.min_value, 0.0f);
    EXPECT_FLOAT_EQ(session.max_value, 0.0f);
    EXPECT_FLOAT_EQ(session.step, 0.0f);
}

TEST(SemanticInteractionSessionTest, InactiveSessionNeverMatchesHit) {
    SemanticInteractionSession session;
    session.active = false;

    SceneHitObject object = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30));

    EXPECT_FALSE(session_matches_hit(session, SemanticHitContentRegion{&object}));
}

TEST(SemanticInteractionSessionTest, ActiveSessionMatchesContentHitWithSameNodeElementRegion) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Click);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    SceneHitObject object = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30));

    EXPECT_TRUE(session_matches_hit(session, SemanticHitContentRegion{&object}));
}

TEST(SemanticInteractionSessionTest, ActiveSessionDoesNotMatchNodeBodyHit) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Click);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    SceneHitObject object = make_node_body(ui::InternedId(10));

    EXPECT_FALSE(session_matches_hit(session, SemanticHitNodeBody{&object}));
}

TEST(SemanticInteractionSessionTest, ActiveSessionDoesNotMatchDifferentRegion) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Click);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    SceneHitObject object = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(99));

    EXPECT_FALSE(session_matches_hit(session, SemanticHitContentRegion{&object}));
}

TEST(SemanticInteractionSessionTest, DragScalarSessionContinuesOnDragAndPreservesIdsKindAction) {
    SemanticInteractionRequest request = make_request(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::InternedId(100),
                                                      InteractionKind::DragScalar);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);

    ASSERT_TRUE(continuation.has_value());
    EXPECT_EQ(continuation->node_id, ui::InternedId(10));
    EXPECT_EQ(continuation->element_id, ui::InternedId(20));
    EXPECT_EQ(continuation->region_id, ui::InternedId(30));
    EXPECT_EQ(continuation->action_id, ui::InternedId(100));
    EXPECT_EQ(continuation->kind, InteractionKind::DragScalar);
}

TEST(SemanticInteractionSessionTest, DragDiscreteSessionContinuesOnDrag) {
    SemanticInteractionRequest request = make_request(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::InternedId(101),
                                                      InteractionKind::DragDiscrete);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);

    ASSERT_TRUE(continuation.has_value());
    EXPECT_EQ(continuation->kind, InteractionKind::DragDiscrete);
}

TEST(SemanticInteractionSessionTest, DragSessionDoesNotContinueOnPressOrRelease) {
    SemanticInteractionRequest request = make_request(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::InternedId(100),
                                                      InteractionKind::DragScalar);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> press_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Press);
    std::optional<SemanticInteractionRequest> release_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Release);

    EXPECT_FALSE(press_continuation.has_value());
    EXPECT_FALSE(release_continuation.has_value());
}

TEST(SemanticInteractionSessionTest, PressSessionContinuesOnlyOnReleaseAndConvertsKind) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Press);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> release_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Release);

    ASSERT_TRUE(release_continuation.has_value());
    EXPECT_EQ(release_continuation->kind, InteractionKind::Release);
    EXPECT_EQ(release_continuation->action_id, ui::InternedId(100));
}

TEST(SemanticInteractionSessionTest, PressSessionDoesNotContinueOnPressOrDrag) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Press);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> press_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Press);
    std::optional<SemanticInteractionRequest> drag_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);

    EXPECT_FALSE(press_continuation.has_value());
    EXPECT_FALSE(drag_continuation.has_value());
}

TEST(SemanticInteractionSessionTest, ClickSessionDoesNotContinue) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Click);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> press_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Press);
    std::optional<SemanticInteractionRequest> drag_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);
    std::optional<SemanticInteractionRequest> release_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Release);

    EXPECT_FALSE(press_continuation.has_value());
    EXPECT_FALSE(drag_continuation.has_value());
    EXPECT_FALSE(release_continuation.has_value());
}

TEST(SemanticInteractionSessionTest, InactiveSessionDoesNotContinue) {
    SemanticInteractionSession session;
    session.active = false;

    std::optional<SemanticInteractionRequest> continuation =
        continue_semantic_interaction_session(session, PointerPhase::Press);

    EXPECT_FALSE(continuation.has_value());
}

TEST(SemanticInteractionSessionTest, ReleaseSessionDoesNotContinue) {
    SemanticInteractionRequest request =
        make_request(ui::InternedId(10), ui::InternedId(20), ui::InternedId(30), ui::InternedId(100),
                     InteractionKind::Release);
    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> press_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Press);
    std::optional<SemanticInteractionRequest> drag_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);
    std::optional<SemanticInteractionRequest> release_continuation =
        continue_semantic_interaction_session(session, PointerPhase::Release);

    EXPECT_FALSE(press_continuation.has_value());
    EXPECT_FALSE(drag_continuation.has_value());
    EXPECT_FALSE(release_continuation.has_value());
}

// Regression: drag continuation must preserve min_value/max_value/step from original request
TEST(SemanticInteractionSessionTest, DragContinuationPreservesRangeAndStep) {
    SemanticInteractionRequest request;
    request.node_id = ui::InternedId(10);
    request.element_id = ui::InternedId(20);
    request.region_id = ui::InternedId(30);
    request.action_id = ui::InternedId(100);
    request.kind = InteractionKind::DragScalar;
    request.min_value = -5.0f;
    request.max_value = 15.0f;
    request.step = 0.25f;

    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    std::optional<SemanticInteractionRequest> continuation =
        continue_semantic_interaction_session(session, PointerPhase::Drag);

    ASSERT_TRUE(continuation.has_value());
    EXPECT_FLOAT_EQ(continuation->min_value, -5.0f);
    EXPECT_FLOAT_EQ(continuation->max_value, 15.0f);
    EXPECT_FLOAT_EQ(continuation->step, 0.25f);
}

// Regression: begin_semantic_interaction_session must copy range fields
TEST(SemanticInteractionSessionTest, BeginSessionCopiesRangeAndStep) {
    SemanticInteractionRequest request;
    request.node_id = ui::InternedId(10);
    request.element_id = ui::InternedId(20);
    request.region_id = ui::InternedId(30);
    request.action_id = ui::InternedId(100);
    request.kind = InteractionKind::DragDiscrete;
    request.min_value = 0.0f;
    request.max_value = 3.0f;
    request.step = 1.0f;

    SemanticInteractionSession session = begin_semantic_interaction_session(request);

    EXPECT_TRUE(session.active);
    EXPECT_FLOAT_EQ(session.min_value, 0.0f);
    EXPECT_FLOAT_EQ(session.max_value, 3.0f);
    EXPECT_FLOAT_EQ(session.step, 1.0f);
}
