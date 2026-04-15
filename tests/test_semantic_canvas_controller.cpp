#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_canvas_controller.h"
#include "semantic_test_helpers.h"

using namespace editor::presentation;
using semantic_test::make_content_region;
using semantic_test::make_binding;

TEST(SemanticCanvasControllerTest, PointerPressProcessesThroughController) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));

    SemanticCanvasControllerResult result = controller.on_pointer_press(ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.step_result.transition, SemanticInputTransition::BeganPress);
    EXPECT_EQ(controller.state(), SemanticInputState::Pressed);
    EXPECT_TRUE(controller.session().active);
}

TEST(SemanticCanvasControllerTest, DragOffHitContinuesDrag) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));

    SemanticCanvasControllerResult begin = controller.on_pointer_drag(ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(begin.step_result.transition, SemanticInputTransition::BeganDrag);

    controller.set_snapshot(SemanticSceneSnapshot{});
    SemanticCanvasControllerResult cont = controller.on_pointer_drag(ui::Pt{150.0f, 150.0f});

    EXPECT_EQ(cont.step_result.transition, SemanticInputTransition::Continued);
    EXPECT_EQ(controller.state(), SemanticInputState::Dragging);
}

TEST(SemanticCanvasControllerTest, ReleaseEndsStateEvenOffHit) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));
    controller.on_pointer_drag(ui::Pt{50.0f, 50.0f});
    ASSERT_EQ(controller.state(), SemanticInputState::Dragging);

    controller.set_snapshot(SemanticSceneSnapshot{});
    SemanticCanvasControllerResult release = controller.on_pointer_release(ui::Pt{150.0f, 150.0f});

    EXPECT_EQ(release.step_result.transition, SemanticInputTransition::EndedRelease);
    EXPECT_EQ(controller.state(), SemanticInputState::Idle);
    EXPECT_FALSE(controller.session().active);
}

TEST(SemanticCanvasControllerTest, CancelDelegatesToSemanticHost) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Press, ui::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));
    controller.on_pointer_press(ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(controller.session().active);

    EXPECT_EQ(controller.cancel(), SemanticInputTransition::Cancelled);
    EXPECT_EQ(controller.state(), SemanticInputState::Idle);
    EXPECT_FALSE(controller.session().active);
}

// == Regression: build_result must map Click → Toggle event ==

TEST(SemanticCanvasControllerTest, ClickProducesToggleControlEvent) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(ui::InternedId(30), InteractionKind::Click, ui::InternedId(200)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));

    SemanticCanvasControllerResult result = controller.on_pointer_press(ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.control_event.kind, SemanticControlEventKind::Toggle);
    EXPECT_EQ(result.control_event.node_id, ui::InternedId(10));
}

// == Regression: build_result must NOT populate scalar/discrete values from binding range ==

TEST(SemanticCanvasControllerTest, DragScalarEventDoesNotLeakMinValueIntoScalarValue) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    InteractionBinding binding = make_binding(ui::InternedId(30), InteractionKind::DragScalar, ui::InternedId(101));
    binding.min_value = 42.0f;
    binding.max_value = 100.0f;
    region.interactions.push_back(binding);
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));

    SemanticCanvasControllerResult result = controller.on_pointer_drag(ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.control_event.kind, SemanticControlEventKind::SetScalar);
    EXPECT_EQ(result.control_event.node_id, ui::InternedId(10));
    // scalar_value must be 0.0 (default) — NOT 42.0 (the binding's min_value)
    EXPECT_FLOAT_EQ(result.control_event.scalar_value, 0.0f);
}

TEST(SemanticCanvasControllerTest, DragDiscreteEventDoesNotLeakMinValueIntoDiscreteValue) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(ui::InternedId(10), ui::InternedId(20),
                                                ui::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    InteractionBinding binding = make_binding(ui::InternedId(30), InteractionKind::DragDiscrete, ui::InternedId(102));
    binding.min_value = 5.0f;
    binding.max_value = 10.0f;
    binding.step = 1.0f;
    region.interactions.push_back(binding);
    snapshot.hit_objects.push_back(region);

    SemanticCanvasController controller;
    controller.set_snapshot(std::move(snapshot));

    SemanticCanvasControllerResult result = controller.on_pointer_drag(ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.control_event.kind, SemanticControlEventKind::SetDiscrete);
    EXPECT_EQ(result.control_event.node_id, ui::InternedId(10));
    // discrete_value must be 0 (default) — NOT 5 (the binding's min_value)
    EXPECT_EQ(result.control_event.discrete_value, 0);
}
