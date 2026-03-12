#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static SlewRate<JitProvider> make_slew_rate(float max_rate = 1.0f, float deadzone = 0.0001f)
{
    SlewRate<JitProvider> comp;
    comp.max_rate = max_rate;
    comp.deadzone = deadzone;
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
// SlewRate Tests
// =============================================================================

TEST(SlewRateTest, ColdStart_FirstFrame)
{
    auto comp = make_slew_rate();
    auto st = make_state(10.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame should instantly set output to input
    EXPECT_FLOAT_EQ(st.across[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(SlewRateTest, LimitsRiseRate)
{
    // max_rate = 10 units/sec, dt = 1/60 sec
    // Max step per frame = 10 * (1/60) = 0.1667
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Step to 10.0 (requires 10 units/sec rise)
    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should only rise by max_rate * dt = 10 * (1/60) ≈ 0.167
    EXPECT_NEAR(st.across[1], 0.167f, 0.001f);
}

TEST(SlewRateTest, LimitsFallRate)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Step to 0.0 (requires 10 units/sec fall)
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should only fall by max_rate * dt = 10 * (1/60) ≈ 0.167
    EXPECT_NEAR(st.across[1], 10.0f - 0.167f, 0.001f);
}

TEST(SlewRateTest, AsymmetricLimits_SameRate)
{
    // Same max_rate for rise and fall
    auto comp = make_slew_rate(5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    st_rise.across[0] = 10.0f;
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    float rise_change = st_rise.across[1];

    // Test fall
    auto comp_fall = make_slew_rate(5.0f);
    auto st_fall = make_state(10.0f);
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    st_fall.across[0] = 0.0f;
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    float fall_change = 10.0f - st_fall.across[1];

    // Rise and fall rates should be equal
    EXPECT_NEAR(rise_change, fall_change, 0.001f);
}

TEST(SlewRateTest, ApproachesTargetOverTime)
{
    auto comp = make_slew_rate(6.0f);  // 6 units/sec = 0.1 units/frame at 60Hz

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;

    for (int i = 0; i < 100; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // After 100 frames (~1.67 seconds at 6 units/sec),
    // should have moved at most 6 * 1.67 = 10 units
    EXPECT_NEAR(st.across[1], 10.0f, 0.1f);
}

TEST(SlewRateTest, HandlesZeroDt_Pause)
{
    auto comp = make_slew_rate();

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    float out_before_pause = st.across[1];

    // Simulate pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 0.0f);
    }

    // Output should not change during pause
    EXPECT_FLOAT_EQ(st.across[1], out_before_pause);
}

TEST(SlewRateTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_slew_rate(10.0f, 0.5f);  // Large deadzone

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    float initial_out = st.across[1];

    // Change input by less than deadzone
    st.across[0] = 5.3f;  // diff = 0.3 < deadzone (0.5)

    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should not have changed
    EXPECT_NEAR(st.across[1], initial_out, 0.001f);
}

TEST(SlewRateTest, Deadzone_AllowsLargeChanges)
{
    auto comp = make_slew_rate(10.0f, 0.5f);

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Change input by MORE than deadzone
    st.across[0] = 10.0f;  // diff = 5.0 > deadzone (0.5)

    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should approach new input
    EXPECT_GT(st.across[1], 6.0f);
}

TEST(SlewRateTest, ZeroRate_NoChange)
{
    auto comp = make_slew_rate(0.0f);

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    float initial_out = st.across[1];

    st.across[0] = 10.0f;

    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // With zero rate, output should not change (except deadzone might affect it)
    EXPECT_NEAR(st.across[1], initial_out, 0.1f);
}

TEST(SlewRateTest, InfiniteRate_InstantTracking)
{
    // Very large rate means essentially no limit
    auto comp = make_slew_rate(100000.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // With very large max_rate, should reach target almost instantly
    EXPECT_NEAR(st.across[1], 10.0f, 0.5f);
}

TEST(SlewRateTest, PreservesStateBetweenFrames)
{
    auto comp = make_slew_rate(6.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    float out1 = st.across[1];

    // Same input, next frame should continue approaching target
    comp.solve_logical(st, 1.0f / 60.0f);
    float out2 = st.across[1];

    EXPECT_GT(out2, out1);
    EXPECT_LT(out2, 10.0f);
}

TEST(SlewRateTest, VariableDt_AdaptsStepSize)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;

    // Small dt = small step
    comp.solve_logical(st, 0.001f);
    float out_small_dt = st.across[1];

    // Large dt = large step
    auto comp2 = make_slew_rate(10.0f);
    auto st2 = make_state(0.0f);
    comp2.solve_logical(st2, 1.0f / 60.0f);
    st2.across[0] = 10.0f;
    comp2.solve_logical(st2, 0.1f);  // 100x larger dt
    float out_large_dt = st2.across[1];

    // Larger dt should result in larger change
    EXPECT_GT(out_large_dt, out_small_dt);
}

TEST(SlewRateTest, NegativeInput_HandlesCorrectly)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = -10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should fall towards -10.0
    EXPECT_LT(st.across[1], 0.0f);
    EXPECT_GT(st.across[1], -1.0f);  // Limited by rate
}

TEST(SlewRateTest, CrossingZero_WorksCorrectly)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = -10.0f;

    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should approach -10.0, crossing zero
    EXPECT_LT(st.across[1], 0.0f);
}

TEST(SlewRateTest, ZeroDeadzone_AllowsAllChanges)
{
    auto comp = make_slew_rate(10.0f, 0.0f);

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Even tiny change should propagate
    st.across[0] = 5.001f;

    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should approach new value
    EXPECT_GT(st.across[1], 5.0005f);
}

TEST(SlewRateTest, ConstantInput_OutputStaysConstant)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    float initial_out = st.across[1];

    // Keep input constant
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should remain constant (in deadzone)
    EXPECT_FLOAT_EQ(st.across[1], initial_out);
}

TEST(SlewRateTest, StepChange_CorrectTotalTime)
{
    // At 60 Hz with max_rate = 60 units/sec:
    // To go from 0 to 60 should take exactly 1 second (60 frames)
    auto comp = make_slew_rate(60.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 60.0f;

    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // After exactly 60 frames at 60 Hz with 60 units/sec rate,
    // should be very close to target
    EXPECT_NEAR(st.across[1], 60.0f, 0.1f);
}
