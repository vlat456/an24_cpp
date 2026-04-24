#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/state.h"


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
// TimeDelay Tests
// =============================================================================

TEST(TimeDelayTest, ColdStart_OutMatchesIn)
{
    auto comp = make_time_delay();
    auto st = make_state(1.0f);

    step(comp, st, 1.0 / 60.0);

    // Cold start: output = input (cold-start-adjusted committed value)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
    EXPECT_FLOAT_EQ(comp.current_out, 1.0f);
}

TEST(TimeDelayTest, TurnOn_DelayOn)
{
    // delay_on = 0.5 seconds
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 0

    // Turn on input
    st.values[0] = 1.0f;

    // Run for 0.4 seconds (less than delay_on)
    for (int i = 0; i < 24; ++i) {  // 24 frames at 60Hz = 0.4s
        step(comp, st, 1.0 / 60.0);
    }

    // Output should still be 0 (delay not expired)
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Run past delay_on (need extra frames for one-frame delay + accumulator reset)
    for (int i = 0; i < 9; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should now be 1 (delay expired and committed)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(TimeDelayTest, TurnOff_DelayOff)
{
    // delay_off = 0.1 seconds
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 1.0

    // Turn off input
    st.values[0] = 0.0f;

    // Run for 0.05 seconds (less than delay_off)
    for (int i = 0; i < 3; ++i) {  // 3 frames ≈ 0.05s
        step(comp, st, 1.0 / 60.0);
    }

    // Output should still be 1 (delay not expired)
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run past delay_off (extra frames for one-frame delay + reset)
    for (int i = 0; i < 6; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should now be 0 (delay expired and committed)
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, InputChanges_ResetsAccumulator)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // cold start

    // Start turning on
    st.values[0] = 1.0f;
    for (int i = 0; i < 20; ++i) {  // 0.33 seconds
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_GT(comp.accumulator, 0.0f);
    EXPECT_LT(comp.accumulator, 0.5f);

    // Toggle input back to 0
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Accumulator should be reset (after commit)
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
}

TEST(TimeDelayTest, RapidToggling_NoOutputChange)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Rapidly toggle input
    for (int i = 0; i < 100; ++i) {
        st.values[0] = (i % 2 == 0) ? 1.0f : 0.0f;
        step(comp, st, 1.0 / 60.0);
    }

    // Output should never have changed (accumulator constantly reset)
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, SymmetricDelays)
{
    // delay_on = delay_off = 0.3 seconds
    auto comp = make_time_delay(0.3f, 0.3f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Test turn on delay (extra frames for one-frame delay + reset)
    st.values[0] = 1.0f;
    for (int i = 0; i < 21; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Test turn off delay
    st.values[0] = 0.0f;
    for (int i = 0; i < 21; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, AsymmetricDelays)
{
    // delay_on = 1.0s (slow), delay_off = 0.1s (fast)
    auto comp = make_time_delay(1.0f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Turn on - takes 1 second (extra frames for one-frame delay + reset)
    st.values[0] = 1.0f;
    for (int i = 0; i < 64; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Turn off - takes only 0.1 second (extra frames for delay + reset)
    st.values[0] = 0.0f;
    for (int i = 0; i < 9; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, ZeroDelay_InstantResponse)
{
    // delay_on = delay_off = 0
    auto comp = make_time_delay(0.0f, 0.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Turn on - with zero delay, timer expires immediately but one-frame delay applies
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages transition
    step(comp, st, 1.0 / 60.0);  // committed transition shows

    // Should respond after one-frame delay
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Turn off
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);  // stages transition
    step(comp, st, 1.0 / 60.0);  // committed transition shows

    // Should respond after one-frame delay
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, LongDelay_TakesFullTime)
{
    // delay_on = 2.0 seconds
    auto comp = make_time_delay(2.0f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 1.0f;

    // Run for 1.9 seconds
    for (int i = 0; i < 114; ++i) {  // 114 frames ≈ 1.9s
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be off
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Run past delay (extra frames for one-frame delay + reset)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should now be on
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(TimeDelayTest, VariableDt_AdaptsAccumulation)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 1.0f;

    // First frame after input change resets accumulator
    step(comp, st, 1.0 / 60.0);

    // Large dt - accumulates
    step(comp, st, 0.25f);

    // Should have accumulated (after commit, accumulator reflects staged value)
    EXPECT_NEAR(comp.accumulator, 0.25f, 0.02f);
}

TEST(TimeDelayTest, HandlesZeroDt_Pause)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 1.0f;

    // Accumulate some time
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    float acc_before_pause = comp.accumulator;

    // Pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    // Accumulator should not have changed
    EXPECT_FLOAT_EQ(comp.accumulator, acc_before_pause);
}

TEST(TimeDelayTest, InputGlitch_Ignored)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Brief glitch to 1.0
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);

    // Immediately back to 0.0
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Accumulator should have been reset
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, AlreadyOn_DelayOffWorks)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 1.0

    // Already on, verify output
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
    EXPECT_FLOAT_EQ(comp.current_out, 1.0f);

    // Turn off input
    st.values[0] = 0.0f;

    // Run for less than delay_off
    for (int i = 0; i < 3; ++i) {  // 0.05s
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be on
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Run to complete delay_off (extra frames for one-frame delay + reset)
    for (int i = 0; i < 6; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should now be off
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, AlreadyOff_DelayOnWorks)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 0

    // Already off, verify output
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Turn on input
    st.values[0] = 1.0f;

    // Run for less than delay_on
    for (int i = 0; i < 20; ++i) {  // 0.33s
        step(comp, st, 1.0 / 60.0);
    }

    // Should still be off
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Run to complete delay_on (extra frames for one-frame delay + reset)
    for (int i = 0; i < 13; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should now be on
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(TimeDelayTest, OutputStableWhenInputStable)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(1.0f);
    step(comp, st, 1.0 / 60.0);

    // Wait with input stable
    st.values[0] = 1.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // Keep input stable
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should remain stable
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(TimeDelayTest, BooleanThreshold_0_5)
{
    auto comp = make_time_delay(0.5f, 0.1f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    // Input below threshold
    st.values[0] = 0.4f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Input above threshold
    st.values[0] = 0.6f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
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
    step(comp, st, 1.0 / 60.0);  // Cold start

    // Run for 2 seconds with input stable at 0 (accumulator grows large)
    for (int i = 0; i < 120; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Now turn on input — delay must start from HERE, not from sim start
    st.values[0] = 1.0f;

    // Run for 0.4 seconds (1 reset frame + 23 accumulation = 23*dt < 0.5)
    for (int i = 0; i < 24; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should STILL be 0: delay_on=0.5s hasn't elapsed since input change
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Run remaining frames to complete delay (extra for one-frame delay)
    for (int i = 0; i < 9; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(TimeDelayTest, Regression_SuccessiveOnOffCycles_IndependentDelays)
{
    // Each ON/OFF transition should independently respect its delay,
    // not carry over accumulated time from the previous phase.
    auto comp = make_time_delay(0.5f, 0.2f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // Cold start

    // --- Cycle 1: Turn ON ---
    st.values[0] = 1.0f;
    // Extra frames for one-frame delay + reset
    for (int i = 0; i < 33; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // --- Cycle 1: Turn OFF ---
    st.values[0] = 0.0f;
    // Extra frames for one-frame delay + reset
    for (int i = 0; i < 15; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // --- Cycle 2: Turn ON again ---
    st.values[0] = 1.0f;
    for (int i = 0; i < 33; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    // --- Cycle 2: Turn OFF again ---
    st.values[0] = 0.0f;
    for (int i = 0; i < 15; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(TimeDelayTest, Regression_AccumulatorResetsOnEveryInputToggle)
{
    // Directly verify the accumulator resets to 0 whenever input changes
    auto comp = make_time_delay(1.0f, 1.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // Cold start

    // Accumulate for 0.5s with input=0
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_GT(comp.accumulator, 0.4f);

    // Change input — accumulator must reset
    st.values[0] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);

    // Accumulate for 0.5s with input=1
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_GT(comp.accumulator, 0.4f);

    // Change input back — accumulator must reset again
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
}
