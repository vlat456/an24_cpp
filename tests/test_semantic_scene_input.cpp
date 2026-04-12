#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_scene_input.h"

using namespace editor::presentation;

namespace {

SceneHitObject make_content_region(ui::InternedId node_id, ui::InternedId element_id,
                                   ui::InternedId region_id, const Rect& bounds) {
    SceneHitObject object;
    object.node_id = node_id;
    object.element_id = element_id;
    object.region_id = region_id;
    object.kind = SceneHitObjectKind::ContentRegion;
    object.bounds = bounds;
    return object;
}

SceneHitObject make_node_body(ui::InternedId node_id, const Rect& bounds) {
    SceneHitObject object;
    object.node_id = node_id;
    object.kind = SceneHitObjectKind::NodeBody;
    object.bounds = bounds;
    return object;
}

InteractionBinding make_binding(InteractionKind kind, ui::InternedId action_id) {
    InteractionBinding binding;
    binding.kind = kind;
    binding.action_id = action_id;
    return binding;
}

} // namespace

// Test 1: empty snapshot returns empty hit, null resolved request, no emitted requests
TEST(SemanticSceneInputTest, EmptySnapshotReturnsEmptyHit) {
    SemanticSceneSnapshot snapshot;
    SemanticInteractionSession session;
    session.active = false;

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(result.hit));
    EXPECT_FALSE(result.resolved_request.has_value());
    EXPECT_EQ(result.reduced.emitted_requests.size(), 0);
    EXPECT_FALSE(result.reduced.next_session.active);
}

// Test 2: clicking a content region resolves a click request and clears session through reducer
TEST(SemanticSceneInputTest, ClickingContentRegionResolvesClickAndClears) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(InteractionKind::Click, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInteractionSession session;
    session.active = false;

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result.hit));
    ASSERT_TRUE(result.resolved_request.has_value());
    EXPECT_EQ(result.resolved_request->kind, InteractionKind::Click);
    EXPECT_EQ(result.resolved_request->action_id, ui::InternedId(100));

    ASSERT_EQ(result.reduced.emitted_requests.size(), 1);
    EXPECT_EQ(result.reduced.emitted_requests[0].kind, InteractionKind::Click);
    EXPECT_FALSE(result.reduced.next_session.active);
}

// Test 3: pressing a press-bound region resolves press request and begins session
TEST(SemanticSceneInputTest, PressingPressRegionBeginsSession) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(InteractionKind::Press, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticInteractionSession session;
    session.active = false;

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result.hit));
    ASSERT_TRUE(result.resolved_request.has_value());
    EXPECT_EQ(result.resolved_request->kind, InteractionKind::Press);

    ASSERT_EQ(result.reduced.emitted_requests.size(), 1);
    EXPECT_TRUE(result.reduced.next_session.active);
    EXPECT_EQ(result.reduced.next_session.kind, InteractionKind::Press);
}

// Test 4: dragging with active drag session and matching hit emits continuation via reducer
TEST(SemanticSceneInputTest, DragContinuationWithMatchingHit) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    // No drag binding on the region itself - this tests continuation via session
    snapshot.hit_objects.push_back(region);

    // Build an active drag session
    SemanticInteractionRequest initial_req;
    initial_req.node_id = ui::InternedId(10);
    initial_req.element_id = ui::InternedId(20);
    initial_req.region_id = ui::InternedId(30);
    initial_req.action_id = ui::InternedId(100);
    initial_req.kind = InteractionKind::DragScalar;
    SemanticInteractionSession session = begin_semantic_interaction_session(initial_req);

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result.hit));
    EXPECT_FALSE(result.resolved_request.has_value()); // No fresh resolved request from bindings
    ASSERT_EQ(result.reduced.emitted_requests.size(), 1);
    EXPECT_EQ(result.reduced.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_TRUE(result.reduced.next_session.active);
}

// Test 5: release with active press session and matching hit emits release continuation and clears
TEST(SemanticSceneInputTest, ReleaseContinuationWithMatchingHitClears) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    snapshot.hit_objects.push_back(region);

    // Build an active press session
    SemanticInteractionRequest initial_req;
    initial_req.node_id = ui::InternedId(10);
    initial_req.element_id = ui::InternedId(20);
    initial_req.region_id = ui::InternedId(30);
    initial_req.action_id = ui::InternedId(100);
    initial_req.kind = InteractionKind::Press;
    SemanticInteractionSession session = begin_semantic_interaction_session(initial_req);

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Release, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result.hit));
    EXPECT_FALSE(result.resolved_request.has_value());
    ASSERT_EQ(result.reduced.emitted_requests.size(), 1);
    EXPECT_EQ(result.reduced.emitted_requests[0].kind, InteractionKind::Release);
    EXPECT_FALSE(result.reduced.next_session.active);
}

// Test 6: node body hit yields node-body hit, null resolved request, and no emitted requests
TEST(SemanticSceneInputTest, NodeBodyHitYieldsNoRequest) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject body = make_node_body(ui::InternedId(10), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    snapshot.hit_objects.push_back(body);

    SemanticInteractionSession session;
    session.active = false;

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitNodeBody>(result.hit));
    EXPECT_FALSE(result.resolved_request.has_value());
    EXPECT_EQ(result.reduced.emitted_requests.size(), 0);
    EXPECT_FALSE(result.reduced.next_session.active);
}

// Test 7: overlapping content region and node body returns content hit and content request path
TEST(SemanticSceneInputTest, OverlappingHitsReturnContentHit) {
    SemanticSceneSnapshot snapshot;

    // Add node body first (lower priority)
    SceneHitObject body = make_node_body(ui::InternedId(10), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    snapshot.hit_objects.push_back(body);

    // Add content region second (higher priority due to reverse iteration in hittest)
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(InteractionKind::Click, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInteractionSession session;
    session.active = false;

    SemanticSceneInputResult result =
        process_semantic_scene_input(snapshot, session, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitContentRegion>(result.hit));
    ASSERT_TRUE(result.resolved_request.has_value());
    EXPECT_EQ(result.resolved_request->kind, InteractionKind::Click);
    ASSERT_EQ(result.reduced.emitted_requests.size(), 1);
}
