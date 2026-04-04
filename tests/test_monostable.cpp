#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static Monostable<JitProvider> make_monostable(float duration = 30.0f)
{
    Monostable<JitProvider> comp;
    comp.duration = duration;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    return comp;
}

static SimulationState make_state(float input_val)
{
    SimulationState st;
    st.values.resize(2, 0.0f);
    st.values[0] = input_val;
    st.values[1] = 0.0f;
    return st;
}

/// Simulate one complete frame: execute + commit (two-phase semantics)
template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

#define step step_component

// =============================================================================
// Monostable Tests
// =============================================================================

TEST(MonostableTest, InitiallyOff)
{
    auto comp = make_monostable();
    auto st = make_state(0.0f);

    step(comp, st, 1.0 / 60.0);

    // Initially output should be off (committed timer = 0)
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
}

TEST(MonostableTest, RisingEdge_TriggersPulse)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Rising edge
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // After commit: timer = duration = 30.0, but output was from committed timer=0 → out=0
    EXPECT_FLOAT_EQ(comp.timer, 30.0f);

    // Next frame: committed timer > 0, so output = 1
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, Pulse_ExpiresAfterDuration)
{
    auto comp = make_monostable(1.0f);  // 1 second pulse

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages timer = duration
    step(comp, st, 1.0 / 60.0);  // output = 1 (committed timer > 0)

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run for 0.9 seconds
    for (int i = 0; i < 54; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run past expiration (extra frames for one-frame delay)
    for (int i = 0; i < 8; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should now be off
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
}

TEST(MonostableTest, FallingEdge_DoesNotRetrigger)
{
    auto comp = make_monostable();

    // Start with input=1 (cold start: last_in=0 by default, so first frame with input=1 IS a rising edge)
    auto st = make_state(1.0f);
    step(comp, st, 1.0 / 60.0);
    // After commit: timer = 30.0 (triggered), output was from committed timer=0 → 0

    // Next frame: committed timer=30.0, output=1
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Input goes low (this is a falling edge, should not retrigger)
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Timer should still be active (not reset by falling edge)
    EXPECT_GT(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, HighInput_DoesNotRetrigger)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // First rising edge
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    float timer_after_first = comp.timer;
    EXPECT_FLOAT_EQ(timer_after_first, 30.0f);

    // Keep input high — no new rising edge, timer just ticks down
    step(comp, st, 1.0 / 60.0);

    // Timer should not be reset (no new rising edge)
    EXPECT_LT(comp.timer, timer_after_first);

    // Output should be 1 (committed timer > 0)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, RetriggerOnNewPulse)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // First trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Let timer decay partially
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_LT(comp.timer, 1.0f);
    EXPECT_GT(comp.timer, 0.0f);

    // Bring input low, then high again (new rising edge)
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Timer should be reset to full duration
    EXPECT_FLOAT_EQ(comp.timer, 1.0f);

    // Output = 1 on next frame (committed timer > 0)
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, VariableDt_TimerTicksCorrectly)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // timer set to 1.0

    // Large dt - timer ticks down
    step(comp, st, 0.5f);

    // Timer should have decreased by 0.5
    EXPECT_NEAR(comp.timer, 0.5f, 0.001f);
    // Output = 1 (committed timer was 1.0 at start of this step → > 0)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Another large dt
    step(comp, st, 0.5f);

    // Timer should now be 0
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    // Output still 1 because committed timer was 0.5 at start of this step → > 0
    // It goes to 0 on next frame when committed timer = 0
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(MonostableTest, ZeroDt_PausesTimer)
{
    auto comp = make_monostable(30.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    float timer_before_pause = comp.timer;

    // Pause
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    // Timer should not have changed
    EXPECT_FLOAT_EQ(comp.timer, timer_before_pause);

    // Output = 1 (committed timer > 0)
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, ShortPulse)
{
    auto comp = make_monostable(0.1f);  // 100ms pulse

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages timer = 0.1

    // Next frame: output = 1 (committed timer > 0)
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run for 0.1 seconds (6 frames)
    for (int i = 0; i < 6; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be on (timer just about to expire)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run a couple more frames to expire (one-frame delay)
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);

    // Should now be off
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(MonostableTest, LongPulse)
{
    auto comp = make_monostable(60.0f);  // 60 second pulse

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages timer = 60.0

    // Run for 1 second
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
    EXPECT_GT(comp.timer, 58.0f);
}

TEST(MonostableTest, BooleanThreshold_0_5)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Input below threshold (not a trigger: 0.4 <= 0.5)
    st.values[0] = 0.4f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);

    // Rising edge through threshold (0.4 → 0.6)
    st.values[0] = 0.6f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(comp.timer, 30.0f);
    // Output shows on next frame
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, NegativeInput_Ignored)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Negative input (not a trigger)
    st.values[0] = -1.0f;
    step(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(MonostableTest, MultiplePulses_Sequential)
{
    auto comp = make_monostable(0.2f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // First pulse
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // Output = 1 on next frame
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Wait for expiration (extra frames for one-frame delay)
    for (int i = 0; i < 15; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_NEAR(st.values[1], 0.0f, 0.01f);

    // Second pulse (need to go low first)
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // Output = 1 on next frame
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(MonostableTest, RapidRetrigger_ExtendsPulse)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // First trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Let decay partially
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_LT(comp.timer, 1.0f);

    // Retrigger
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Timer should be back to full duration
    EXPECT_FLOAT_EQ(comp.timer, 1.0f);
}

TEST(MonostableTest, ZeroDuration_InstantPulse)
{
    auto comp = make_monostable(0.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // With zero duration, timer immediately set to 0 (trigger ? 0.0 : max(0, 0-dt) = 0)
    // Actually: trigger ? duration : ... → timer = 0.0 (duration is 0)
    // Output from committed timer=0 → 0
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
}

TEST(MonostableTest, TimerClampedAtZero)
{
    auto comp = make_monostable(0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Run past expiration
    for (int i = 0; i < 20; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Timer should be exactly zero (not negative)
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(MonostableTest, InputHighDuringPulse_NoEffect)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Trigger
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    float timer_after_trigger = comp.timer;

    // Keep input high for several frames (no re-trigger since no new rising edge)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Timer should have decreased, not reset
    EXPECT_LT(comp.timer, timer_after_trigger);
    EXPECT_GT(comp.timer, 0.0f);
}

TEST(MonostableTest, EngineStartCycle_RealisticUseCase)
{
    // 30 second engine start cycle
    auto comp = make_monostable(30.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Press start button
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages timer = 30.0

    // Output = 1 on next frame
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Simulate 10 seconds of cranking
    for (int i = 0; i < 600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be cranking
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
    EXPECT_GT(comp.timer, 19.0f);

    // Release start button (goes low)
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Cranking continues automatically
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Simulate another 20 seconds
    for (int i = 0; i < 1200; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should now stop
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}
