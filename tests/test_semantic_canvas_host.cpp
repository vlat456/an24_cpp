#include <gtest/gtest.h>

#include "editor/visual/presentation/semantic_canvas_host.h"
#include "semantic_test_helpers.h"

using namespace editor::presentation;
using semantic_test::make_content_region;
using semantic_test::make_binding;

TEST(SemanticCanvasHostTest, StartsIdleWithEmptySnapshot) {
    SemanticCanvasHost host;

    EXPECT_EQ(host.state(), SemanticInputState::Idle);
    EXPECT_FALSE(host.session().active);
    EXPECT_TRUE(host.snapshot().hit_objects.empty());
}

TEST(SemanticCanvasHostTest, StepUsesOwnedSnapshot) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(core::InternedId(10), core::InternedId(20),
                                                core::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(core::InternedId(30), InteractionKind::Press, core::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasHost host;
    host.set_snapshot(std::move(snapshot));

    SemanticInputMachineStepResult result = host.step(PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_EQ(result.transition, SemanticInputTransition::BeganPress);
    EXPECT_EQ(host.state(), SemanticInputState::Pressed);
    EXPECT_TRUE(host.session().active);
}

TEST(SemanticCanvasHostTest, ReplacingSnapshotChangesFutureHitResults) {
    SemanticSceneSnapshot first;
    SceneHitObject region = make_content_region(core::InternedId(10), core::InternedId(20),
                                                core::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(core::InternedId(30), InteractionKind::Click, core::InternedId(100)));
    first.hit_objects.push_back(region);

    SemanticCanvasHost host;
    host.set_snapshot(std::move(first));
    SemanticInputMachineStepResult hit = host.step(PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(hit.scene_result.resolved_request.has_value());

    SemanticSceneSnapshot second;
    host.set_snapshot(std::move(second));
    SemanticInputMachineStepResult miss = host.step(PointerPhase::Press, ui::Pt{50.0f, 50.0f});

    EXPECT_TRUE(std::holds_alternative<SemanticHitEmpty>(miss.scene_result.hit));
    EXPECT_FALSE(miss.scene_result.resolved_request.has_value());
}

TEST(SemanticCanvasHostTest, CancelDelegatesToMachine) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(core::InternedId(10), core::InternedId(20),
                                                core::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(core::InternedId(30), InteractionKind::Press, core::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasHost host;
    host.set_snapshot(std::move(snapshot));
    host.step(PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(host.session().active);

    EXPECT_EQ(host.cancel(), SemanticInputTransition::Cancelled);
    EXPECT_EQ(host.state(), SemanticInputState::Idle);
    EXPECT_FALSE(host.session().active);
}

TEST(SemanticCanvasHostTest, ResetDelegatesToMachine) {
    SemanticSceneSnapshot snapshot;
    SceneHitObject region = make_content_region(core::InternedId(10), core::InternedId(20),
                                                core::InternedId(30), ui::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    region.interactions.push_back(make_binding(core::InternedId(30), InteractionKind::Press, core::InternedId(100)));
    snapshot.hit_objects.push_back(region);

    SemanticCanvasHost host;
    host.set_snapshot(std::move(snapshot));
    host.step(PointerPhase::Press, ui::Pt{50.0f, 50.0f});
    ASSERT_TRUE(host.session().active);

    host.reset();

    EXPECT_EQ(host.state(), SemanticInputState::Idle);
    EXPECT_FALSE(host.session().active);
}
