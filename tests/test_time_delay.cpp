#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static TimeDelay<JitProvider> make_time_delay(float delay_on = 0.5f, float delay_off = 0.1f)
{
    TimeDelay<JitProvider> comp;
    comp.delay_on = delay_on;
    comp.delay_off = delay_off;
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
// TimeDelay Tests
// =============================================================================

TEST(TimeDelayTest, ColdStart_OutMatchesIn)
{
    auto comp = make_time_delay();
    auto st = make_state(1.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame: output should match input
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
    EXPECT_FLOAT_EQ(comp.current_out, 1.0f);
}

TEST(TimeDelayTest, TurnOn_DelayOn)
{
    // delay_on = 0.5 seconds
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn on input
    st.across[0] = 1.0f;

    // Run for 0.4 seconds (less than delay_on)
    for (int i = 0; i < 24; ++i) {  // 24 frames at 60Hz = 0.4s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should still be 0 (delay not expired)
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Run past delay_on (need +1 frame for accumulator reset on input change)
    for (int i = 0; i < 7; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should now be 1 (delay expired)
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(TimeDelayTest, TurnOff_DelayOff)
{
    // delay_off = 0.1 seconds
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn off input
    st.across[0] = 0.0f;

    // Run for 0.05 seconds (less than delay_off)
    for (int i = 0; i < 3; ++i) {  // 3 frames ≈ 0.05s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should still be 1 (delay not expired)
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Run past delay_off (+1 frame for accumulator reset on input change)
    for (int i = 0; i < 4; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should now be 0 (delay expired)
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, InputChanges_ResetsAccumulator)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Start turning on
    st.across[0] = 1.0f;
    for (int i = 0; i < 20; ++i) {  // 0.33 seconds
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_GT(comp.accumulator, 0.0f);
    EXPECT_LT(comp.accumulator, 0.5f);

    // Toggle input back to 0
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Accumulator should be reset
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
}

TEST(TimeDelayTest, RapidToggling_NoOutputChange)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Rapidly toggle input
    for (int i = 0; i < 100; ++i) {
        st.across[0] = (i % 2 == 0) ? 1.0f : 0.0f;
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should never have changed (accumulator constantly reset)
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, SymmetricDelays)
{
    // delay_on = delay_off = 0.3 seconds
    auto comp = make_time_delay(0.3f, 0.3f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Test turn on delay (+1 frame for reset)
    st.across[0] = 1.0f;
    for (int i = 0; i < 19; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Test turn off delay (+1 frame for reset)
    st.across[0] = 0.0f;
    for (int i = 0; i < 19; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, AsymmetricDelays)
{
    // delay_on = 1.0s (slow), delay_off = 0.1s (fast)
    auto comp = make_time_delay(1.0f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn on - takes 1 second (+2 frames: 1 reset + 1 fp margin)
    st.across[0] = 1.0f;
    for (int i = 0; i < 62; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Turn off - takes only 0.1 second (+1 frame for reset)
    st.across[0] = 0.0f;
    for (int i = 0; i < 7; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, ZeroDelay_InstantResponse)
{
    // delay_on = delay_off = 0
    auto comp = make_time_delay(0.0f, 0.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn on
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should respond instantly
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Turn off
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should respond instantly
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, LongDelay_TakesFullTime)
{
    // delay_on = 2.0 seconds
    auto comp = make_time_delay(2.0f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 1.0f;

    // Run for 1.9 seconds
    for (int i = 0; i < 114; ++i) {  // 114 frames ≈ 1.9s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be off
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Run past delay (+2 frames: 1 reset + 1 fp margin)
    for (int i = 0; i < 8; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should now be on
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(TimeDelayTest, VariableDt_AdaptsAccumulation)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 1.0f;

    // First frame after input change resets accumulator
    comp.solve_logical(st, 1.0f / 60.0f);

    // Large dt - accumulates from zero
    comp.solve_logical(st, 0.25f);

    // Should have accumulated 0.25 seconds
    EXPECT_NEAR(comp.accumulator, 0.25f, 0.001f);
}

TEST(TimeDelayTest, HandlesZeroDt_Pause)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 1.0f;

    // Accumulate some time
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    float acc_before_pause = comp.accumulator;

    // Pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 0.0f);
    }

    // Accumulator should not have changed
    EXPECT_FLOAT_EQ(comp.accumulator, acc_before_pause);
}

TEST(TimeDelayTest, InputGlitch_Ignored)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Brief glitch to 1.0
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Immediately back to 0.0
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Accumulator should have been reset
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, AlreadyOn_DelayOffWorks)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Already on, verify output
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
    EXPECT_FLOAT_EQ(comp.current_out, 1.0f);

    // Turn off input
    st.across[0] = 0.0f;

    // Run for less than delay_off
    for (int i = 0; i < 3; ++i) {  // 0.05s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Run to complete delay_off (+1 frame for reset)
    for (int i = 0; i < 4; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should now be off
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, AlreadyOff_DelayOnWorks)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Already off, verify output
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Turn on input
    st.across[0] = 1.0f;

    // Run for less than delay_on
    for (int i = 0; i < 20; ++i) {  // 0.33s
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should still be off
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Run to complete delay_on (+1 frame for reset, total 31 frames = 0.5s)
    for (int i = 0; i < 11; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should now be on
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(TimeDelayTest, OutputStableWhenInputStable)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Wait for delay to expire
    st.across[0] = 1.0f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // Keep input stable
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should remain stable
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(TimeDelayTest, BooleanThreshold_0_5)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Input below threshold
    st.across[0] = 0.4f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Input above threshold
    st.across[0] = 0.6f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(TimeDelayTest, Regression_DelayStartsFromInputChange_NotSimStart)
{
    // Verify the accumulator resets on input change, so delay is measured
    // from the moment of change, not from simulation start.
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);  // Cold start

    // Run for 2 seconds with input stable at 0 (accumulator grows large)
    for (int i = 0; i < 120; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Now turn on input — delay must start from HERE, not from sim start
    st.across[0] = 1.0f;

    // Run for 0.4 seconds (1 reset frame + 23 accumulation = 23*dt < 0.5)
    for (int i = 0; i < 24; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should STILL be 0: delay_on=0.5s hasn't elapsed since input change
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // Run remaining frames to complete delay (7 more = 30 accumulation = 0.5s)
    for (int i = 0; i < 7; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(TimeDelayTest, Regression_SuccessiveOnOffCycles_IndependentDelays)
{
    // Each ON/OFF transition should independently respect its delay,
    // not carry over accumulated time from the previous phase.
    auto comp = make_time_delay(0.5f, 0.2f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);  // Cold start

    // --- Cycle 1: Turn ON ---
    st.across[0] = 1.0f;
    // 1 reset + 30 accumulation = 30*dt = 0.5s
    for (int i = 0; i < 31; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // --- Cycle 1: Turn OFF ---
    st.across[0] = 0.0f;
    // 1 reset + 12 accumulation = 12*dt = 0.2s
    for (int i = 0; i < 13; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);

    // --- Cycle 2: Turn ON again ---
    st.across[0] = 1.0f;
    for (int i = 0; i < 31; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 1.0f);

    // --- Cycle 2: Turn OFF again ---
    st.across[0] = 0.0f;
    for (int i = 0; i < 13; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(TimeDelayTest, Regression_AccumulatorResetsOnEveryInputToggle)
{
    // Directly verify the accumulator resets to 0 whenever input changes
    auto comp = make_time_delay(1.0f, 1.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);  // Cold start

    // Accumulate for 0.5s with input=0
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_GT(comp.accumulator, 0.4f);

    // Change input — accumulator must reset
    st.across[0] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);

    // Accumulate for 0.5s with input=1
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_GT(comp.accumulator, 0.4f);

    // Change input back — accumulator must reset again
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
}
