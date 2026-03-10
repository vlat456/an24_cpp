#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

// =============================================================================
// Test Helpers
// =============================================================================

static AsymSlewRate<JitProvider> make_asym_slew_rate(float rate_up = 1.0f, float rate_down = 0.5f, float deadzone = 0.0001f)
{
    AsymSlewRate<JitProvider> comp;
    comp.rate_up = rate_up;
    comp.rate_down = rate_down;
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
// AsymSlewRate Tests
// =============================================================================

TEST(AsymSlewRateTest, ColdStart_FirstFrame)
{
    auto comp = make_asym_slew_rate();
    auto st = make_state(10.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First frame should instantly set output to input
    EXPECT_FLOAT_EQ(st.across[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(AsymSlewRateTest, AsymmetricRates_RiseFasterThanFall)
{
    // rate_up = 10 units/sec, rate_down = 5 units/sec
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    st_rise.across[0] = 10.0f;
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    float rise_change = st_rise.across[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(10.0f, 5.0f);
    auto st_fall = make_state(10.0f);
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    st_fall.across[0] = 0.0f;
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    float fall_change = 10.0f - st_fall.across[1];

    // Rise should be faster than fall
    EXPECT_GT(rise_change, fall_change);
}

TEST(AsymSlewRateTest, AsymmetricRates_RiseSlowerThanFall)
{
    // rate_up = 5 units/sec, rate_down = 10 units/sec
    auto comp = make_asym_slew_rate(5.0f, 10.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    st_rise.across[0] = 10.0f;
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    float rise_change = st_rise.across[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(5.0f, 10.0f);
    auto st_fall = make_state(10.0f);
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    st_fall.across[0] = 0.0f;
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    float fall_change = 10.0f - st_fall.across[1];

    // Fall should be faster than rise
    EXPECT_LT(rise_change, fall_change);
}

TEST(AsymSlewRateTest, SymmetricRates_EqualChange)
{
    // rate_up = rate_down = 5 units/sec
    auto comp = make_asym_slew_rate(5.0f, 5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    st_rise.across[0] = 10.0f;
    comp.solve_logical(st_rise, 1.0f / 60.0f);
    float rise_change = st_rise.across[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(5.0f, 5.0f);
    auto st_fall = make_state(10.0f);
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    st_fall.across[0] = 0.0f;
    comp_fall.solve_logical(st_fall, 1.0f / 60.0f);
    float fall_change = 10.0f - st_fall.across[1];

    // Rise and fall should be equal
    EXPECT_NEAR(rise_change, fall_change, 0.001f);
}

TEST(AsymSlewRateTest, RapidRiseSlowFall_RealisticBehavior)
{
    // Simulates relay/indicator: fast turn-on, slow turn-off
    // rate_up = 100 (instant), rate_down = 2 (slow decay)
    auto comp = make_asym_slew_rate(100.0f, 2.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn on
    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 10.0f, 0.5f);  // Should reach almost instantly

    // Turn off
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    float after_fall = st.across[1];

    // Fall should be much slower than rise
    EXPECT_GT(after_fall, 8.0f);  // Still high after one frame
}

TEST(AsymSlewRateTest, SlowRiseRapidFall_RealisticBehavior)
{
    // Simulates capacitor discharge: slow charge, fast discharge
    // rate_up = 2 (slow), rate_down = 100 (instant)
    auto comp = make_asym_slew_rate(2.0f, 100.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Turn on (slow rise)
    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_LT(st.across[1], 1.0f);  // Barely moved

    // Turn off (fast fall)
    auto comp2 = make_asym_slew_rate(2.0f, 100.0f);
    auto st2 = make_state(10.0f);
    comp2.solve_logical(st2, 1.0f / 60.0f);
    st2.across[0] = 0.0f;
    comp2.solve_logical(st2, 1.0f / 60.0f);

    // Fall should be near instant
    EXPECT_NEAR(st2.across[1], 0.0f, 0.5f);
}

TEST(AsymSlewRateTest, HandlesZeroDt_Pause)
{
    auto comp = make_asym_slew_rate();

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

TEST(AsymSlewRateTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_asym_slew_rate(1.0f, 0.5f, 0.5f);  // Large deadzone

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

TEST(AsymSlewRateTest, Deadzone_AllowsLargeChanges)
{
    auto comp = make_asym_slew_rate(1.0f, 0.5f, 0.5f);

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

TEST(AsymSlewRateTest, ZeroRates_NoChange)
{
    auto comp = make_asym_slew_rate(0.0f, 0.0f);

    auto st = make_state(5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    float initial_out = st.across[1];

    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // With zero rates, output should not change
    EXPECT_NEAR(st.across[1], initial_out, 0.001f);
}

TEST(AsymSlewRateTest, ApproachesTargetOverTime_Rise)
{
    auto comp = make_asym_slew_rate(6.0f, 3.0f);  // Different rates

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;

    for (int i = 0; i < 120; ++i) {  // 2 seconds at 60Hz
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should reach target (using rate_up = 6)
    EXPECT_NEAR(st.across[1], 10.0f, 0.1f);
}

TEST(AsymSlewRateTest, ApproachesTargetOverTime_Fall)
{
    auto comp = make_asym_slew_rate(6.0f, 3.0f);

    auto st = make_state(10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 0.0f;

    for (int i = 0; i < 240; ++i) {  // 4 seconds at 60Hz (slower rate)
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should reach target (using rate_down = 3)
    EXPECT_NEAR(st.across[1], 0.0f, 0.1f);
}

TEST(AsymSlewRateTest, VariableDt_AdaptsStepSize)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 10.0f;

    // Small dt = small step
    comp.solve_logical(st, 0.001f);
    float out_small_dt = st.across[1];

    // Large dt = large step
    auto comp2 = make_asym_slew_rate(10.0f, 5.0f);
    auto st2 = make_state(0.0f);
    comp2.solve_logical(st2, 1.0f / 60.0f);
    st2.across[0] = 10.0f;
    comp2.solve_logical(st2, 0.1f);  // 100x larger dt
    float out_large_dt = st2.across[1];

    // Larger dt should result in larger change
    EXPECT_GT(out_large_dt, out_small_dt);
}

TEST(AsymSlewRateTest, NegativeInput_Rise)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = -10.0f;  // Rising (going more negative)
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should rise towards -10.0 using rate_up
    EXPECT_LT(st.across[1], 0.0f);
    EXPECT_GT(st.across[1], -1.0f);  // Limited by rate
}

TEST(AsymSlewRateTest, NegativeInput_Fall)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(-10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 0.0f;  // Falling (going less negative)
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should fall towards 0.0 using rate_down (slower)
    EXPECT_LT(st.across[1], -8.0f);  // Barely moved (rate_down = 5)
}

TEST(AsymSlewRateTest, ConstantInput_OutputStaysConstant)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

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

TEST(AsymSlewRateTest, OscillatingInput_FollowsAsymmetricRates)
{
    // Test with alternating input
    auto comp = make_asym_slew_rate(60.0f, 30.0f);

    auto st = make_state(0.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    // Rise to 10
    st.across[0] = 10.0f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[1], 10.0f, 0.1f);

    // Fall to 0 (slower)
    st.across[0] = 0.0f;
    for (int i = 0; i < 60; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    // Should only be halfway down (rate_down = 30, half of rate_up)
    EXPECT_NEAR(st.across[1], 5.0f, 0.5f);
}
