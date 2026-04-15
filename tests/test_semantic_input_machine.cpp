#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_input_machine.h"
#include "semantic_test_helpers.h"

using namespace editor::presentation;
using semantic_test::make_content_region;
using semantic_test::make_binding;

TEST(SemanticInputMachineTest, StartsWithInactiveSession) {
    SemanticInputMachine machine;

    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

TEST(SemanticInputMachineTest, PressStepBeginsPersistentSession) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    SemanticInputMachineStepResult result = machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.previous_state, SemanticInputState::Idle);
    EXPECT_EQ(result.next_state, SemanticInputState::Pressed);
    EXPECT_EQ(result.transition, SemanticInputTransition::BeganPress);
    ASSERT_EQ(result.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(machine.state(), SemanticInputState::Pressed);
    EXPECT_TRUE(machine.session().active);
    EXPECT_EQ(machine.session().node_id, ui::InternedId(10));
    EXPECT_EQ(machine.session().region_id, ui::InternedId(30));
}

TEST(SemanticInputMachineTest, DragStepUsesStoredSessionContinuation) {
    SemanticSceneSnapshot begin_snapshot;
    SceneHitObject begin_region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    begin_region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    begin_snapshot.hit_objects.push_back(begin_region);

    SemanticSceneSnapshot continue_snapshot;
    SceneHitObject continue_region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                         ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    continue_snapshot.hit_objects.push_back(continue_region);

    SemanticInputMachine machine;

    SemanticInputMachineStepResult begin = machine.step(begin_snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    EXPECT_EQ(begin.previous_state, SemanticInputState::Idle);
    EXPECT_EQ(begin.next_state, SemanticInputState::Dragging);
    EXPECT_EQ(begin.transition, SemanticInputTransition::BeganDrag);
    ASSERT_EQ(begin.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(begin.scene_result.reduced.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);
    ASSERT_TRUE(machine.session().active);

    SemanticInputMachineStepResult cont = machine.step(continue_snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    EXPECT_EQ(cont.previous_state, SemanticInputState::Dragging);
    EXPECT_EQ(cont.next_state, SemanticInputState::Dragging);
    EXPECT_EQ(cont.transition, SemanticInputTransition::Continued);
    ASSERT_EQ(cont.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_FALSE(cont.scene_result.resolved_request.has_value());
    EXPECT_EQ(cont.scene_result.reduced.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);
    EXPECT_TRUE(machine.session().active);
}

TEST(SemanticInputMachineTest, ReleaseStepClearsPersistentSession) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(machine.session().active);

    SemanticInputMachineStepResult result = machine.step(snapshot, PointerPhase::Release, ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.previous_state, SemanticInputState::Pressed);
    EXPECT_EQ(result.next_state, SemanticInputState::Idle);
    EXPECT_EQ(result.transition, SemanticInputTransition::EndedRelease);
    ASSERT_EQ(result.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(result.scene_result.reduced.emitted_requests[0].kind, InteractionKind::Release);
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

TEST(SemanticInputMachineTest, ResetClearsSession) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(machine.session().active);

    machine.reset();

    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

TEST(SemanticInputMachineTest, CancelClearsSessionAndReportsTransition) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(machine.session().active);

    EXPECT_EQ(machine.cancel(), SemanticInputTransition::Cancelled);
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

TEST(SemanticInputMachineTest, CancelFromIdleReturnsNone) {
    SemanticInputMachine machine;

    EXPECT_EQ(machine.cancel(), SemanticInputTransition::None);
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
}

TEST(SemanticInputMachineTest, EmptySnapshotLeavesSessionInactive) {
    SemanticInputMachine machine;
    SemanticSceneSnapshot snapshot;

    SemanticInputMachineStepResult result = machine.step(snapshot, PointerPhase::Press, ui::Pt{0.0f, 0.0f});

    EXPECT_EQ(result.previous_state, SemanticInputState::Idle);
    EXPECT_EQ(result.next_state, SemanticInputState::Idle);
    EXPECT_EQ(result.transition, SemanticInputTransition::None);
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(result.scene_result.hit));
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
    EXPECT_TRUE(result.scene_result.reduced.emitted_requests.empty());
}

// == Regression: Pressed→Dragging must produce BeganDrag, not Continued ==

TEST(SemanticInputMachineTest, PressedToDragTransitionProducesBeganDrag) {
    // Region with both Press and DragScalar bindings.
    // Press → Pressed. Then Drag → should BeganDrag (not Continued).
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;

    // Step 1: Press → BeganPress, state=Pressed
    SemanticInputMachineStepResult press = machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(press.transition, SemanticInputTransition::BeganPress);
    ASSERT_EQ(machine.state(), SemanticInputState::Pressed);

    // Step 2: Drag → BeganDrag (NOT Continued), state=Dragging
    SemanticInputMachineStepResult drag = machine.step(snapshot, PointerPhase::Drag, ui::Pt{55.0f, 50.0f});
    EXPECT_EQ(drag.transition, SemanticInputTransition::BeganDrag);
    ASSERT_EQ(drag.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(drag.scene_result.reduced.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);
    EXPECT_TRUE(machine.session().active);
}

// == Cancel from Dragging ==

TEST(SemanticInputMachineTest, CancelFromDraggingReturnsCancelled) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(machine.state(), SemanticInputState::Dragging);
    ASSERT_TRUE(machine.session().active);

    EXPECT_EQ(machine.cancel(), SemanticInputTransition::Cancelled);
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

// == Release from Dragging ==

TEST(SemanticInputMachineTest, ReleaseFromDraggingClearsSession) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(machine.state(), SemanticInputState::Dragging);

    SemanticInputMachineStepResult release = machine.step(snapshot, PointerPhase::Release, ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

// == Click interaction: transition=None but request still emitted ==

TEST(SemanticInputMachineTest, ClickProducesNoneTransitionButEmitsRequest) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Click, ui::InternedId(200)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    SemanticInputMachineStepResult result = machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.transition, SemanticInputTransition::None);
    ASSERT_EQ(result.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(result.scene_result.reduced.emitted_requests[0].kind, InteractionKind::Click);
    EXPECT_EQ(result.scene_result.reduced.emitted_requests[0].action_id, ui::InternedId(200));
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}

// == Drag on a Pressed-only region: no emission, stays Pressed ==

TEST(SemanticInputMachineTest, DragOnPressOnlyRegionStaysPressedNoEmission) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(machine.state(), SemanticInputState::Pressed);

    SemanticInputMachineStepResult drag = machine.step(snapshot, PointerPhase::Drag, ui::Pt{55.0f, 50.0f});

    EXPECT_EQ(drag.transition, SemanticInputTransition::None);
    EXPECT_TRUE(drag.scene_result.reduced.emitted_requests.empty());
    EXPECT_EQ(machine.state(), SemanticInputState::Pressed);
    EXPECT_TRUE(machine.session().active);
}

TEST(SemanticInputMachineTest, DragOffHitKeepsDraggingSessionAndContinues) {
    SemanticSceneSnapshot begin_snapshot;
    SceneHitObject begin_region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    begin_region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    begin_snapshot.hit_objects.push_back(begin_region);

    SemanticSceneSnapshot empty_snapshot;
    SemanticInputMachine machine;

    SemanticInputMachineStepResult begin = machine.step(begin_snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(begin.transition, SemanticInputTransition::BeganDrag);
    ASSERT_EQ(machine.state(), SemanticInputState::Dragging);

    SemanticInputMachineStepResult cont = machine.step(empty_snapshot, PointerPhase::Drag, ui::Pt{150.0f, 150.0f});
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(cont.scene_result.hit));
    EXPECT_EQ(cont.previous_state, SemanticInputState::Dragging);
    EXPECT_EQ(cont.next_state, SemanticInputState::Dragging);
    EXPECT_EQ(cont.transition, SemanticInputTransition::Continued);
    ASSERT_EQ(cont.scene_result.reduced.emitted_requests.size(), 1u);
    EXPECT_EQ(cont.scene_result.reduced.emitted_requests[0].kind, InteractionKind::DragScalar);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);
}

TEST(SemanticInputMachineTest, ReleaseOffHitEndsDraggingSession) {
    SemanticSceneSnapshot begin_snapshot;
    SceneHitObject begin_region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                      ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    begin_region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    begin_snapshot.hit_objects.push_back(begin_region);

    SemanticSceneSnapshot empty_snapshot;
    SemanticInputMachine machine;
    machine.step(begin_snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(machine.state(), SemanticInputState::Dragging);

    SemanticInputMachineStepResult release = machine.step(empty_snapshot, PointerPhase::Release, ui::Pt{150.0f, 150.0f});
    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(release.scene_result.hit));
    EXPECT_EQ(release.previous_state, SemanticInputState::Dragging);
    EXPECT_EQ(release.next_state, SemanticInputState::Idle);
    EXPECT_EQ(release.transition, SemanticInputTransition::EndedRelease);
    EXPECT_FALSE(machine.session().active);
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
}

// == Step after reset returns to clean Idle behavior ==

TEST(SemanticInputMachineTest, StepAfterResetBehavesAsIdle) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;
    machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(machine.state(), SemanticInputState::Pressed);

    machine.reset();

    // Fresh press after reset should produce BeganPress, not Continued
    SemanticInputMachineStepResult result = machine.step(snapshot, PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    EXPECT_EQ(result.transition, SemanticInputTransition::BeganPress);
    EXPECT_EQ(machine.state(), SemanticInputState::Pressed);
}

// == Full drag lifecycle: BeganDrag → Continued → Continued → EndedRelease ==

TEST(SemanticInputMachineTest, FullDragLifecycle) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticInputMachine machine;

    // Begin drag
    SemanticInputMachineStepResult r1 = machine.step(snapshot, PointerPhase::Drag, ui::Pt{50.0f, 50.0f});
    EXPECT_EQ(r1.transition, SemanticInputTransition::BeganDrag);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);

    // Continue drag (with same snapshot — session carries forward)
    SemanticInputMachineStepResult r2 = machine.step(snapshot, PointerPhase::Drag, ui::Pt{60.0f, 50.0f});
    EXPECT_EQ(r2.transition, SemanticInputTransition::Continued);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);

    // Continue drag again
    SemanticInputMachineStepResult r3 = machine.step(snapshot, PointerPhase::Drag, ui::Pt{70.0f, 50.0f});
    EXPECT_EQ(r3.transition, SemanticInputTransition::Continued);
    EXPECT_EQ(machine.state(), SemanticInputState::Dragging);

    // Release
    SemanticInputMachineStepResult r4 = machine.step(snapshot, PointerPhase::Release, ui::Pt{70.0f, 50.0f});
    EXPECT_EQ(machine.state(), SemanticInputState::Idle);
    EXPECT_FALSE(machine.session().active);
}
