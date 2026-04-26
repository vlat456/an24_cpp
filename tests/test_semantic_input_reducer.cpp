#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_input_reducer.h"

using namespace editor::presentation;

namespace {

SemanticInteractionRequest make_request(core::InternedId node_id, core::InternedId element_id,
                                        core::InternedId region_id, core::InternedId action_id,
                                        InteractionKind kind) {
    SemanticInteractionRequest request;
    request.node_id = node_id;
    request.element_id = element_id;
    request.region_id = region_id;
    request.action_id = action_id;
    request.kind = kind;
    return request;
}

SceneHitObject make_content_region(core::InternedId node_id, core::InternedId element_id,
                                   core::InternedId region_id) {
    SceneHitObject object;
    object.node_id = node_id;
    object.element_id = element_id;
    object.region_id = region_id;
    object.kind = SceneHitObjectKind::ContentRegion;
    return object;
}

SceneHitObject make_node_body(core::InternedId node_id) {
    SceneHitObject object;
    object.node_id = node_id;
    object.kind = SceneHitObjectKind::NodeBody;
    return object;
}

} // namespace

// Test 1: resolved click emits request and clears session
TEST(SemanticInputReducerTest, ResolvedClickEmitsRequestAndClearsSession) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInteractionRequest resolved_req =
        make_request(core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(100),
                     InteractionKind::Click);

    SemanticInputStepResult result = reduce_semantic_input(
        current_session, PointerPhase::Press, SemanticHitEmpty{}, resolved_req);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].action_id, core::InternedId(100));
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::Click);
    EXPECT_FALSE(result.next_session.active);
}

// Test 2: resolved press emits request and begins session
TEST(SemanticInputReducerTest, ResolvedPressEmitsRequestAndBeginsSession) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInteractionRequest resolved_req =
        make_request(core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(101),
                     InteractionKind::Press);

    SemanticInputStepResult result = reduce_semantic_input(
        current_session, PointerPhase::Press, SemanticHitEmpty{}, resolved_req);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].action_id, core::InternedId(101));
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::Press);
    EXPECT_TRUE(result.next_session.active);
    EXPECT_EQ(result.next_session.action_id, core::InternedId(101));
    EXPECT_EQ(result.next_session.kind, InteractionKind::Press);
}

// Test 3: resolved drag-scalar emits request and begins session
TEST(SemanticInputReducerTest, ResolvedDragScalarEmitsRequestAndBeginsSession) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInteractionRequest resolved_req =
        make_request(core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(102),
                     InteractionKind::DragScalar);

    SemanticInputStepResult result = reduce_semantic_input(
        current_session, PointerPhase::Drag, SemanticHitEmpty{}, resolved_req);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_TRUE(result.next_session.active);
    EXPECT_EQ(result.next_session.kind, InteractionKind::DragScalar);
}

// Test 4: resolved drag-discrete emits request and begins session
TEST(SemanticInputReducerTest, ResolvedDragDiscreteEmitsRequestAndBeginsSession) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInteractionRequest resolved_req =
        make_request(core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(103),
                     InteractionKind::DragDiscrete);

    SemanticInputStepResult result = reduce_semantic_input(
        current_session, PointerPhase::Drag, SemanticHitEmpty{}, resolved_req);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::DragDiscrete);
    EXPECT_TRUE(result.next_session.active);
    EXPECT_EQ(result.next_session.kind, InteractionKind::DragDiscrete);
}

// Test 5: session continuation on drag emits request and keeps session active
TEST(SemanticInputReducerTest, SessionContinuationOnDragEmitsRequestAndKeepsActive) {
    SemanticInteractionRequest initial_req = make_request(
        core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(100),
        InteractionKind::DragScalar);
    SemanticInteractionSession current_session = begin_semantic_interaction_session(initial_req);

    SceneHitObject object = make_content_region(core::InternedId(10), core::InternedId(20), core::InternedId(30));
    SemanticHitContentRegion hit{&object};

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Drag, hit, std::nullopt);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_TRUE(result.next_session.active);
    EXPECT_EQ(result.next_session.kind, InteractionKind::DragScalar);
}

// Test 6: press session continuation on release emits release request and clears session
TEST(SemanticInputReducerTest, PressContinuationOnReleaseEmitsAndClears) {
    SemanticInteractionRequest initial_req = make_request(
        core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(100),
        InteractionKind::Press);
    SemanticInteractionSession current_session = begin_semantic_interaction_session(initial_req);

    SceneHitObject object = make_content_region(core::InternedId(10), core::InternedId(20), core::InternedId(30));
    SemanticHitContentRegion hit{&object};

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Release, hit, std::nullopt);

    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::Release);
    EXPECT_FALSE(result.next_session.active);
}

// Test 7: release with no request or continuation clears session
TEST(SemanticInputReducerTest, ReleaseWithNoContinuationClearsSession) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Release, SemanticHitEmpty{}, std::nullopt);

    EXPECT_EQ(result.emitted_requests.size(), 0);
    EXPECT_FALSE(result.next_session.active);
}

// Test 8: inactive session with no request produces no output and stays inactive
TEST(SemanticInputReducerTest, InactiveSessionWithNoRequestStaysInactive) {
    SemanticInteractionSession current_session;
    current_session.active = false;

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Press, SemanticHitEmpty{}, std::nullopt);

    EXPECT_EQ(result.emitted_requests.size(), 0);
    EXPECT_FALSE(result.next_session.active);
}

// Test 9: active session with non-matching hit and no continuation stays unchanged
TEST(SemanticInputReducerTest, ActiveSessionWithNonMatchingHitStaysUnchanged) {
    SemanticInteractionRequest initial_req = make_request(
        core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(100),
        InteractionKind::Click);
    SemanticInteractionSession current_session = begin_semantic_interaction_session(initial_req);

    // Different region in hit
    SceneHitObject object = make_content_region(core::InternedId(10), core::InternedId(20), core::InternedId(99));
    SemanticHitContentRegion hit{&object};

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Press, hit, std::nullopt);

    EXPECT_EQ(result.emitted_requests.size(), 0);
    EXPECT_TRUE(result.next_session.active);
    EXPECT_EQ(result.next_session.action_id, core::InternedId(100));
}

// Test 10: resolved request takes priority over continuation when both are possible
TEST(SemanticInputReducerTest, ResolvedRequestTakesPriorityOverContinuation) {
    SemanticInteractionRequest initial_req = make_request(
        core::InternedId(10), core::InternedId(20), core::InternedId(30), core::InternedId(100),
        InteractionKind::Press);
    SemanticInteractionSession current_session = begin_semantic_interaction_session(initial_req);

    // Resolved request is different from potential continuation
    SemanticInteractionRequest resolved_req = make_request(
        core::InternedId(40), core::InternedId(50), core::InternedId(60), core::InternedId(200),
        InteractionKind::Click);

    SceneHitObject object = make_content_region(core::InternedId(10), core::InternedId(20), core::InternedId(30));
    SemanticHitContentRegion hit{&object};

    SemanticInputStepResult result =
        reduce_semantic_input(current_session, PointerPhase::Release, hit, resolved_req);

    // Should emit the resolved request, not the continuation
    ASSERT_EQ(result.emitted_requests.size(), 1);
    EXPECT_EQ(result.emitted_requests[0].action_id, core::InternedId(200));
    EXPECT_EQ(result.emitted_requests[0].kind, InteractionKind::Click);
    EXPECT_FALSE(result.next_session.active);
}
