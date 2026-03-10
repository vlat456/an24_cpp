#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

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
    st.across.resize(3, 0.0f);
    st.through.resize(3, 0.0f);
    st.conductance.resize(3, 0.0f);
    st.across[0] = input_val;
    st.across[1] = reset_val;
    st.across[2] = 0.0f;
    return st;
}

// =============================================================================
// Integrator Tests
// =============================================================================

TEST(IntegratorTest, ColdStart_StartsAtInitialValue)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame should initialize to initial_val
    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
    EXPECT_FLOAT_EQ(comp.accumulator, 5.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(IntegratorTest, Integration_AccumulatesPositiveInput)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 1 second at 10 units/sec (59 more frames after cold start)
    for (int i = 0; i < 59; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 10.0 (10 * 1.0)
    EXPECT_NEAR(st.across[2], 10.0f, 0.1f);
}

TEST(IntegratorTest, Integration_AccumulatesNegativeInput)
{
    auto comp = make_integrator(1.0f, 100.0f);
    auto st = make_state(-5.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 2 seconds at -5 units/sec
    for (int i = 0; i < 120; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 90.0 (100 + (-5 * 2.0))
    EXPECT_NEAR(st.across[2], 90.0f, 0.1f);
}

TEST(IntegratorTest, Reset_ZerosAccumulator)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Accumulate some value
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_GT(st.across[2], 5.0f);

    // Reset
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should be zero
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
    EXPECT_FLOAT_EQ(comp.accumulator, 0.0f);
}

TEST(IntegratorTest, ResetWhileHigh_StaysZero)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 1.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // With reset active, should stay at 0
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(IntegratorTest, ResetReleased_ResumesIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 1.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Reset active, no accumulation
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);

    // Release reset
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Now should accumulate (59 more frames after reset release)
    for (int i = 0; i < 59; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_NEAR(st.across[2], 10.0f, 0.1f);
}

TEST(IntegratorTest, Gain_ScalesIntegration)
{
    auto comp = make_integrator(2.0f, 0.0f);  // gain = 2
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 1 second (59 more frames)
    for (int i = 0; i < 59; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 20.0 (10 * 2 * 1.0)
    EXPECT_NEAR(st.across[2], 20.0f, 0.1f);
}

TEST(IntegratorTest, NegativeGain_InvertsIntegration)
{
    auto comp = make_integrator(-1.0f, 100.0f);  // Negative gain
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 1 second (59 more frames)
    for (int i = 0; i < 59; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 90.0 (100 + (10 * -1 * 1.0))
    EXPECT_NEAR(st.across[2], 90.0f, 0.1f);
}

TEST(IntegratorTest, ZeroGain_NoAccumulation)
{
    auto comp = make_integrator(0.0f, 5.0f);  // gain = 0
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should stay at initial value
    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(IntegratorTest, VariableDt_AdaptsIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);  // Cold start frame

    // Large dt
    comp.solve_logical(st, 0.5f);

    // Should accumulate 10 * 0.5 = 5.0 (plus tiny bit from cold start frame)
    EXPECT_NEAR(st.across[2], 5.0f, 0.2f);
}

TEST(IntegratorTest, ZeroDt_NoAccumulation)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    float before_pause = st.across[2];

    // Pause
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 0.0f);
    }

    // Should not have changed
    EXPECT_FLOAT_EQ(st.across[2], before_pause);
}

TEST(IntegratorTest, ZeroInput_NoAccumulation)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate zero
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should stay at initial value
    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(IntegratorTest, Precision_MaintainedOverTime)
{
    auto comp = make_integrator(0.001f, 0.0f);  // Small gain
    auto st = make_state(1000.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 60 seconds
    for (int i = 0; i < 3600; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be 60.0 (1000 * 0.001 * 60)
    EXPECT_NEAR(st.across[2], 60.0f, 0.5f);
}

TEST(IntegratorTest, FuelConsumption_RealisticUseCase)
{
    // Simulate fuel consumption calculation
    // Input: flow rate in liters/sec (negative for consumption)
    // Output: total remaining fuel in liters

    auto comp = make_integrator(1.0f, 100.0f);  // Start with 100L
    auto st = make_state(-0.5f, 0.0f);  // 0.5 L/sec consumption (negative input)

    comp.solve_logical(st, 1.0f / 60.0f);

    // Run for 10 seconds at 0.5 L/sec (599 more frames after cold start)
    for (int i = 0; i < 599; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 95.0L (100 + (-0.5) * 10)
    EXPECT_NEAR(st.across[2], 95.0f, 0.5f);

    // Increase consumption to 1.0 L/sec (599 more frames)
    st.across[0] = -1.0f;
    for (int i = 0; i < 599; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 85.0L (95 + (-1.0) * 10)
    EXPECT_NEAR(st.across[2], 85.0f, 0.5f);
}

TEST(IntegratorTest, BatteryCharge_RealisticUseCase)
{
    // Simulate battery charging
    // Input: current in Amps (positive = charging)
    // gain = 1.0, dt already in seconds
    // Output: accumulated charge in Amp-seconds

    auto comp = make_integrator(1.0f, 0.0f);  // Start empty
    auto st = make_state(10.0f, 0.0f);  // 10A charging

    comp.solve_logical(st, 1.0f / 60.0f);

    // Charge for 1 minute at 10A (3599 more frames = 60 seconds)
    for (int i = 0; i < 3599; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 600 A-sec (10A * 60sec)
    EXPECT_NEAR(st.across[2], 600.0f, 1.0f);
}

TEST(IntegratorTest, WearAccumulation_RealisticUseCase)
{
    // Simulate component wear accumulation
    // Input: load factor (0-1), represents proportion of wear
    // Output: accumulated wear in seconds

    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(0.8f, 0.0f);  // 80% load

    comp.solve_logical(st, 1.0f / 60.0f);

    // Run for 1 minute at 80% load (3599 more frames = 60 seconds)
    for (int i = 0; i < 3599; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should be approximately 48 seconds of wear (0.8 * 60sec)
    EXPECT_NEAR(st.across[2], 48.0f, 0.5f);
}

TEST(IntegratorTest, BooleanThreshold_Reset)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Accumulate
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_GT(st.across[2], 5.0f);

    // Below threshold (no reset)
    st.across[1] = 0.4f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_GT(st.across[2], 5.0f);

    // Above threshold (reset)
    st.across[1] = 0.6f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(IntegratorTest, LargeDt_Clip)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(1.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Very large dt (simulates lag spike)
    comp.solve_logical(st, 10.0f);

    // Should handle gracefully
    EXPECT_NEAR(st.across[2], 10.0f, 0.5f);
}

TEST(IntegratorTest, MultipleResets)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First accumulation
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    float acc1 = st.across[2];
    EXPECT_GT(acc1, 0.0f);

    // First reset
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);

    // Release reset
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Second accumulation
    for (int i = 0; i < 30; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    float acc2 = st.across[2];
    EXPECT_NEAR(acc2, acc1, 0.01f);

    // Second reset
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(IntegratorTest, NegativeInputCrossesZero)
{
    auto comp = make_integrator(1.0f, 50.0f);
    auto st = make_state(-10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Integrate for 6 seconds
    for (int i = 0; i < 360; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should cross zero and go negative
    EXPECT_LT(st.across[2], 0.0f);
    EXPECT_NEAR(st.across[2], -10.0f, 0.5f);
}

TEST(IntegratorTest, AlternatingInput_CorrectIntegration)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Positive for 1 second
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[2], 10.0f, 0.5f);

    // Negative for 1 second
    st.across[0] = -10.0f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[2], 0.0f, 0.5f);

    // Positive again for 1 second
    st.across[0] = 10.0f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[2], 10.0f, 0.5f);
}

TEST(IntegratorTest, ResetDoesNotAffectFirstFrameMask)
{
    auto comp = make_integrator(1.0f, 5.0f);
    auto st = make_state(10.0f, 1.0f);  // Reset active from start

    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame should initialize even with reset, but reset overrides it
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);

    // Reset keeps it at zero (even though initial_val was 5.0)
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(IntegratorTest, IntegrationContinuesAfterReset)
{
    auto comp = make_integrator(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Accumulate
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[2], 10.0f, 0.5f);

    // Reset
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);

    // Release reset and continue
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should have accumulated again
    EXPECT_NEAR(st.across[2], 10.0f, 0.5f);
}
