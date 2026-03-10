#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

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
    st.across.resize(2, 0.0f);
    st.through.resize(2, 0.0f);
    st.conductance.resize(2, 0.0f);
    st.across[0] = input_val;
    st.across[1] = 0.0f;
    return st;
}

// =============================================================================
// Monostable Tests
// =============================================================================

TEST(MonostableTest, InitiallyOff)
{
    auto comp = make_monostable();
    auto st = make_state(0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Initially output should be off
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
}

TEST(MonostableTest, RisingEdge_TriggersPulse)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Rising edge
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Timer should be set to duration, output should be on
    EXPECT_FLOAT_EQ(comp.timer, 30.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, Pulse_ExpiresAfterDuration)
{
    auto comp = make_monostable(1.0f);  // 1 second pulse

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Run for 0.9 seconds
    for (int i = 0; i < 54; ++i) {  // 54 frames ≈ 0.9s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Run for 0.1 more seconds (total 1.0s)
    for (int i = 0; i < 6; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should now be off (with small tolerance for floating point)
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);
    EXPECT_NEAR(comp.timer, 0.0f, 0.001f);
}

TEST(MonostableTest, FallingEdge_DoesNotRetrigger)
{
    auto comp = make_monostable();

    auto st = make_state(1.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame with input=1.0 triggers the timer
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Input goes low (this is a falling edge, should not retrigger)
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Timer should still be active (not reset by falling edge)
    EXPECT_GT(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, HighInput_DoesNotRetrigger)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // First rising edge
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    float timer_after_first = comp.timer;
    EXPECT_FLOAT_EQ(timer_after_first, 30.0f);

    // Keep input high
    comp.solve_logical(st, 1.0f / 60.0f);

    // Timer should not be reset
    EXPECT_LT(comp.timer, timer_after_first);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, RetriggerOnNewPulse)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // First trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Let timer decay partially
    for (int i = 0; i < 30; ++i) {  // 0.5 seconds
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_LT(comp.timer, 1.0f);
    EXPECT_GT(comp.timer, 0.0f);

    // Bring input low, then high again
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Timer should be reset to full duration
    EXPECT_FLOAT_EQ(comp.timer, 1.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, VariableDt_TimerTicksCorrectly)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Large dt
    comp.solve_logical(st, 0.5f);

    // Timer should have decreased by 0.5
    EXPECT_NEAR(comp.timer, 0.5f, 0.001f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Another large dt
    comp.solve_logical(st, 0.5f);

    // Timer should now be 0
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(MonostableTest, ZeroDt_PausesTimer)
{
    auto comp = make_monostable(30.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    float timer_before_pause = comp.timer;

    // Pause
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 0.0f);
    }

    // Timer should not have changed
    EXPECT_FLOAT_EQ(comp.timer, timer_before_pause);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, ShortPulse)
{
    auto comp = make_monostable(0.1f);  // 100ms pulse

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Run for 0.1 seconds (6 frames)
    for (int i = 0; i < 6; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be on (timer just about to expire)
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // One more frame to actually expire
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should now be off
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(MonostableTest, LongPulse)
{
    auto comp = make_monostable(60.0f);  // 60 second pulse

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Run for 1 second
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
    EXPECT_GT(comp.timer, 58.0f);
}

TEST(MonostableTest, BooleanThreshold_0_5)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Input below threshold (not a trigger)
    st.across[0] = 0.4f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);

    // Rising edge through threshold (trigger)
    st.across[0] = 0.6f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(comp.timer, 30.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, NegativeInput_Ignored)
{
    auto comp = make_monostable();

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Negative input (not a trigger)
    st.across[0] = -1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(MonostableTest, MultiplePulses_Sequential)
{
    auto comp = make_monostable(0.2f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // First pulse
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Wait for expiration (13 frames for 0.2s due to floating point)
    for (int i = 0; i < 13; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);

    // Second pulse (need to go low first)
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(MonostableTest, RapidRetrigger_ExtendsPulse)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // First trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Let decay partially
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_LT(comp.timer, 1.0f);

    // Retrigger
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Timer should be back to full duration
    EXPECT_FLOAT_EQ(comp.timer, 1.0f);
}

TEST(MonostableTest, ZeroDuration_InstantPulse)
{
    auto comp = make_monostable(0.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // With zero duration, output turns on but immediately expires
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
}

TEST(MonostableTest, TimerClampedAtZero)
{
    auto comp = make_monostable(0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Run past expiration
    for (int i = 0; i < 20; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Timer should be exactly zero (not negative)
    EXPECT_FLOAT_EQ(comp.timer, 0.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(MonostableTest, InputHighDuringPulse_NoEffect)
{
    auto comp = make_monostable(1.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    float timer_after_trigger = comp.timer;

    // Keep input high for several frames
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
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
    comp.solve_logical(st, 1.0f / 60.0f);

    // Press start button
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Simulate 10 seconds of cranking
    for (int i = 0; i < 600; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be cranking
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
    EXPECT_GT(comp.timer, 19.0f);

    // Release start button (goes low)
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Cranking continues automatically
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Simulate another 20 seconds
    for (int i = 0; i < 1200; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should now stop
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}
