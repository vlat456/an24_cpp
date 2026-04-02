#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static Integrator<JitProvider> make_integrator(float gain = 1.0f, float initial_val = 0.0f)
{
    Integrator<JitProvider> comp;
    comp.gain = gain;
    comp.initial_val = initial_val;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::reset, 1);
    comp.provider.set(PortNames::out, 2);
    return comp;
}

static SimulationState make_state(float input_val, float reset_val)
{
    SimulationState st;
    st.values.resize(3, 0.0f);
    st.values[0] = input_val;
    st.values[1] = reset_val;
    st.values[2] = 0.0f;
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
// Integrator Tests
// =============================================================================

TEST(IntegratorTest, ColdStart_StartsAtInitialValue)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Cold start: output = committed accumulator (cold-start-adjusted to initial_val = 5.0)
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
    // After commit, accumulator = next_accumulator = initial_val + input*gain*dt = 5.0 + 0 = 5.0
    EXPECT_FLOAT_EQ(comp.accumulator, 5.0f);
}

TEST(IntegratorTest, Integration_AccumulatesPositiveInput)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 0.0

    // Integrate for 1 second at 10 units/sec (60 more frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 10.0 (10 * 1.0)
    EXPECT_NEAR(st.values[2], 10.0f, 0.2f);
}

TEST(IntegratorTest, Integration_AccumulatesNegativeInput)
{
    auto comp = make_integrator(1.0f, 100.0f);
    auto st = make_state(-5.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 100.0

    // Integrate for 2 seconds at -5 units/sec (120 frames)
    for (int i = 0; i < 120; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 90.0 (100 + (-5 * 2.0))
    EXPECT_NEAR(st.values[2], 90.0f, 0.2f);
}

TEST(IntegratorTest, Reset_ZerosAccumulator)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Accumulate some value
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_GT(st.values[2], 5.0f);

    // Reset
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // Output = committed accumulator (pre-reset value from last frame)
    // After commit, accumulator = 0 (reset applied)
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);

    // Next frame: output shows 0
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(IntegratorTest, ResetWhileHigh_StaysZero)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 1.0f);

    step(comp, st, 1.0 / 60.0);

    // With reset active, should stay at 0
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(IntegratorTest, ResetReleased_ResumesIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 1.0f);

    step(comp, st, 1.0 / 60.0);

    // Reset active, no accumulation
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);

    // Release reset
    st.values[1] = 0.0f;

    // Now should accumulate (60 more frames = 1 second)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);
}

TEST(IntegratorTest, Gain_ScalesIntegration)
{
    auto comp = make_integrator(2.0f, 0.0f);  // gain = 2
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start

    // Integrate for 1 second (60 frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 20.0 (10 * 2 * 1.0)
    EXPECT_NEAR(st.values[2], 20.0f, 0.5f);
}

TEST(IntegratorTest, NegativeGain_InvertsIntegration)
{
    auto comp = make_integrator(-1.0f, 100.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 100.0

    // Integrate for 1 second (60 frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 90.0 (100 + (10 * -1 * 1.0))
    EXPECT_NEAR(st.values[2], 90.0f, 0.5f);
}

TEST(IntegratorTest, ZeroGain_NoAccumulation)
{
    auto comp = make_integrator(0.0f, 5.0f);  // gain = 0
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 5.0

    // Integrate
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should stay at initial value
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);
}

TEST(IntegratorTest, VariableDt_AdaptsIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // Cold start frame, output = 0

    // Large dt
    step(comp, st, 0.5f);
    // Output = committed accumulator (0 + 10*1/60 ≈ 0.167)
    // After commit, accumulator ≈ 0.167 + 10*0.5 = 5.167

    // Next frame shows committed value
    step(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 5.0f, 0.5f);
}

TEST(IntegratorTest, ZeroDt_NoAccumulation)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(10.0f, 0.0f);

    // Run initial steps to let the integrator accumulate
    step(comp, st, 1.0 / 60.0);  // cold start
    step(comp, st, 1.0 / 60.0);

    // Set input to 0 so no more accumulation, then flush pipeline
    st.values[0] = 0.0f;
    step(comp, st, 0.0f);  // stages zero-accumulation, outputs old committed value
    step(comp, st, 0.0f);  // now output = committed value with no pending accumulation

    float before_pause = st.values[2];

    // Pause (dt=0 should not accumulate)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    // Output should not have changed during pause
    EXPECT_FLOAT_EQ(st.values[2], before_pause);
}

TEST(IntegratorTest, ZeroInput_NoAccumulation)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Integrate zero
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should stay at initial value
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);
}

TEST(IntegratorTest, Precision_MaintainedOverTime)
{
    auto comp = make_integrator(0.001f, 0.0f);  // Small gain
    auto st = make_state(1000.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Integrate for 60 seconds (3600 frames)
    for (int i = 0; i < 3600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be 60.0 (1000 * 0.001 * 60)
    EXPECT_NEAR(st.values[2], 60.0f, 0.5f);
}

TEST(IntegratorTest, FuelConsumption_RealisticUseCase)
{
    auto comp = make_integrator(1.0f, 100.0f);  // Start with 100L
    auto st = make_state(-0.5f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 100.0

    // Run for 10 seconds at 0.5 L/sec (600 frames)
    for (int i = 0; i < 600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 95.0L (100 + (-0.5) * 10)
    EXPECT_NEAR(st.values[2], 95.0f, 0.5f);

    // Increase consumption to 1.0 L/sec (600 more frames)
    st.values[0] = -1.0f;
    for (int i = 0; i < 600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 85.0L (95 + (-1.0) * 10)
    EXPECT_NEAR(st.values[2], 85.0f, 0.5f);
}

TEST(IntegratorTest, BatteryCharge_RealisticUseCase)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start

    // Charge for 1 minute at 10A (3600 frames)
    for (int i = 0; i < 3600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 600 A-sec (10A * 60sec)
    EXPECT_NEAR(st.values[2], 600.0f, 1.0f);
}

TEST(IntegratorTest, WearAccumulation_RealisticUseCase)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(0.8f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start

    // Run for 1 minute at 80% load (3600 frames)
    for (int i = 0; i < 3600; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should be approximately 48 seconds of wear (0.8 * 60sec)
    EXPECT_NEAR(st.values[2], 48.0f, 0.5f);
}

TEST(IntegratorTest, BooleanThreshold_Reset)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Accumulate
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_GT(st.values[2], 5.0f);

    // Below threshold (no reset: 0.4 <= 0.5)
    st.values[1] = 0.4f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_GT(st.values[2], 5.0f);

    // Above threshold (reset: 0.6 > 0.5)
    st.values[1] = 0.6f;
    step(comp, st, 1.0 / 60.0);
    // Committed accumulator was pre-reset, output = pre-reset value
    // After commit, accumulator = 0

    // Next frame: output shows 0
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(IntegratorTest, LargeDt_Clip)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(1.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 0

    // Very large dt (simulates lag spike)
    step(comp, st, 10.0f);
    // Output = committed accumulator (≈ 1/60 from first frame)
    // After commit, accumulator = previous + 1.0 * 10.0 = 10.0167

    // Next frame shows committed value
    step(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);
}

TEST(IntegratorTest, MultipleResets)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // First accumulation
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    float acc1 = st.values[2];
    EXPECT_GT(acc1, 0.0f);

    // First reset
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // After commit, accumulator = 0

    // Next frame: output = 0
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);

    // Release reset
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Second accumulation
    for (int i = 0; i < 30; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    float acc2 = st.values[2];
    EXPECT_NEAR(acc2, acc1, 0.1f);

    // Second reset
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(IntegratorTest, NegativeInputCrossesZero)
{
    auto comp = make_integrator(1.0f, 50.0f);
    auto st = make_state(-10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start, output = 50

    // Integrate for 6 seconds (360 frames)
    for (int i = 0; i < 360; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should cross zero and go negative (50 + (-10)*6 = -10)
    EXPECT_LT(st.values[2], 0.0f);
    EXPECT_NEAR(st.values[2], -10.0f, 0.5f);
}

TEST(IntegratorTest, AlternatingInput_CorrectIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start

    // Positive for 1 second (60 frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);

    // Negative for 1 second
    st.values[0] = -10.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_NEAR(st.values[2], 0.0f, 0.5f);

    // Positive again for 1 second
    st.values[0] = 10.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);
}

TEST(IntegratorTest, ResetDoesNotAffectFirstFrameMask)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(10.0f, 1.0f);  // Reset active from start

    step(comp, st, 1.0 / 60.0);

    // First frame should consume first_frame_mask even with reset
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);

    // Reset keeps it at zero (even though initial_val was 5.0)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(IntegratorTest, IntegrationContinuesAfterReset)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);  // cold start

    // Accumulate for 1 second
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0 / 60.0);
    }
    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);

    // Reset
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);

    // Release reset and continue
    st.values[1] = 0.0f;
    for (int i = 0; i < 62; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should have accumulated again
    EXPECT_NEAR(st.values[2], 10.0f, 0.5f);
}
