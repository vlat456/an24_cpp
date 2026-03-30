#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


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
    st.values.resize(2, 0.0f);
    st.values[0] = input_val;
    st.values[1] = 0.0f;
    return st;
}

/// Simulate one complete frame: execute + commit (two-phase semantics)
template <typename Comp>
void step_component(Comp& comp, SimulationState& st, float dt) {
    comp.execute(st, dt);
    comp.commit(st);
}

#define step step_component

// =============================================================================
// AsymSlewRate Tests
// =============================================================================

TEST(AsymSlewRateTest, ColdStart_FirstFrame)
{
    auto comp = make_asym_slew_rate();
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);

    // Cold-start: output = cold-start-adjusted committed value = 10.0
    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(AsymSlewRateTest, AsymmetricRates_RiseFasterThanFall)
{
    // rate_up = 10 units/sec, rate_down = 5 units/sec
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    step(comp, st_rise, 1.0f / 60.0f);  // cold start at 0
    st_rise.values[0] = 10.0f;
    step(comp, st_rise, 1.0f / 60.0f);  // output = 0 (committed), stages rise step
    step(comp, st_rise, 1.0f / 60.0f);  // output = first rise step
    float rise_change = st_rise.values[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(10.0f, 5.0f);
    auto st_fall = make_state(10.0f);
    step(comp_fall, st_fall, 1.0f / 60.0f);  // cold start at 10
    st_fall.values[0] = 0.0f;
    step(comp_fall, st_fall, 1.0f / 60.0f);  // output = 10 (committed), stages fall step
    step(comp_fall, st_fall, 1.0f / 60.0f);  // output = first fall step
    float fall_change = 10.0f - st_fall.values[1];

    // Rise should be faster than fall
    EXPECT_GT(rise_change, fall_change);
}

TEST(AsymSlewRateTest, AsymmetricRates_RiseSlowerThanFall)
{
    // rate_up = 5 units/sec, rate_down = 10 units/sec
    auto comp = make_asym_slew_rate(5.0f, 10.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    step(comp, st_rise, 1.0f / 60.0f);
    st_rise.values[0] = 10.0f;
    step(comp, st_rise, 1.0f / 60.0f);  // stages rise step
    step(comp, st_rise, 1.0f / 60.0f);  // outputs first rise step
    float rise_change = st_rise.values[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(5.0f, 10.0f);
    auto st_fall = make_state(10.0f);
    step(comp_fall, st_fall, 1.0f / 60.0f);
    st_fall.values[0] = 0.0f;
    step(comp_fall, st_fall, 1.0f / 60.0f);  // stages fall step
    step(comp_fall, st_fall, 1.0f / 60.0f);  // outputs first fall step
    float fall_change = 10.0f - st_fall.values[1];

    // Fall should be faster than rise
    EXPECT_LT(rise_change, fall_change);
}

TEST(AsymSlewRateTest, SymmetricRates_EqualChange)
{
    // rate_up = rate_down = 5 units/sec
    auto comp = make_asym_slew_rate(5.0f, 5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    step(comp, st_rise, 1.0f / 60.0f);
    st_rise.values[0] = 10.0f;
    step(comp, st_rise, 1.0f / 60.0f);  // stages rise step
    step(comp, st_rise, 1.0f / 60.0f);  // outputs first rise step
    float rise_change = st_rise.values[1];

    // Test fall
    auto comp_fall = make_asym_slew_rate(5.0f, 5.0f);
    auto st_fall = make_state(10.0f);
    step(comp_fall, st_fall, 1.0f / 60.0f);
    st_fall.values[0] = 0.0f;
    step(comp_fall, st_fall, 1.0f / 60.0f);  // stages fall step
    step(comp_fall, st_fall, 1.0f / 60.0f);  // outputs first fall step
    float fall_change = 10.0f - st_fall.values[1];

    // Rise and fall should be equal
    EXPECT_NEAR(rise_change, fall_change, 0.001f);
}

TEST(AsymSlewRateTest, RapidRiseSlowFall_RealisticBehavior)
{
    // Simulates relay/indicator: fast turn-on, slow turn-off
    // rate_up = 1000 (instant in 1 frame), rate_down = 2 (slow decay)
    auto comp = make_asym_slew_rate(1000.0f, 2.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    // Turn on
    st.values[0] = 10.0f;
    step(comp, st, 1.0f / 60.0f);  // output = 0 (committed), stages ~10
    step(comp, st, 1.0f / 60.0f);  // output = ~10 (committed)
    EXPECT_NEAR(st.values[1], 10.0f, 0.5f);  // Should reach almost instantly after delay

    // Turn off
    st.values[0] = 0.0f;
    step(comp, st, 1.0f / 60.0f);  // output = committed (~10), stages fall step
    float after_fall = st.values[1];

    // One-frame delay: output still shows ~10
    EXPECT_GT(after_fall, 8.0f);  // Still high (output is committed from before fall)
}

TEST(AsymSlewRateTest, SlowRiseRapidFall_RealisticBehavior)
{
    // Simulates capacitor discharge: slow charge, fast discharge
    // rate_up = 2 (slow), rate_down = 1000 (instant in 1 frame)
    auto comp = make_asym_slew_rate(2.0f, 1000.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    // Turn on (slow rise)
    st.values[0] = 10.0f;
    step(comp, st, 1.0f / 60.0f);  // output = 0 (committed), stages small step
    step(comp, st, 1.0f / 60.0f);  // output = small step (~0.033)
    EXPECT_LT(st.values[1], 1.0f);  // Barely moved after one-frame delay

    // Turn off (fast fall) - fresh component starting at 10
    auto comp2 = make_asym_slew_rate(2.0f, 1000.0f);
    auto st2 = make_state(10.0f);
    step(comp2, st2, 1.0f / 60.0f);  // cold start at 10
    st2.values[0] = 0.0f;
    step(comp2, st2, 1.0f / 60.0f);  // output = 10 (committed), stages ~0
    step(comp2, st2, 1.0f / 60.0f);  // output = ~0 (committed)

    // Fall should be near instant after one-frame delay
    EXPECT_NEAR(st2.values[1], 0.0f, 0.5f);
}

TEST(AsymSlewRateTest, HandlesZeroDt_Pause)
{
    auto comp = make_asym_slew_rate();

    auto st = make_state(5.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 5

    float out_before_pause = st.values[1];

    // Simulate pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    // Output should not change during pause
    EXPECT_FLOAT_EQ(st.values[1], out_before_pause);
}

TEST(AsymSlewRateTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_asym_slew_rate(1.0f, 0.5f, 0.5f);  // Large deadzone

    auto st = make_state(5.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 5

    float initial_out = st.values[1];

    // Change input by less than deadzone
    st.values[0] = 5.3f;  // diff = 0.3 < deadzone (0.5)

    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Output should not have changed
    EXPECT_NEAR(st.values[1], initial_out, 0.001f);
}

TEST(AsymSlewRateTest, Deadzone_AllowsLargeChanges)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f, 0.5f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 5

    // Change input by MORE than deadzone
    st.values[0] = 10.0f;  // diff = 5.0 > deadzone (0.5)

    for (int i = 0; i < 12; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Output should approach new input (rate_up=10 → ~0.167/frame × 11 effective = ~1.83)
    EXPECT_GT(st.values[1], 6.0f);
}

TEST(AsymSlewRateTest, ZeroRates_NoChange)
{
    auto comp = make_asym_slew_rate(0.0f, 0.0f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 5

    float initial_out = st.values[1];

    st.values[0] = 10.0f;
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // With zero rates, output should not change
    EXPECT_NEAR(st.values[1], initial_out, 0.001f);
}

TEST(AsymSlewRateTest, ApproachesTargetOverTime_Rise)
{
    auto comp = make_asym_slew_rate(6.0f, 3.0f);  // Different rates

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    st.values[0] = 10.0f;

    // Need extra frames for one-frame delay
    for (int i = 0; i < 122; ++i) {  // ~2 seconds at 60Hz
        step(comp, st, 1.0f / 60.0f);
    }

    // Should reach target (using rate_up = 6)
    EXPECT_NEAR(st.values[1], 10.0f, 0.2f);
}

TEST(AsymSlewRateTest, ApproachesTargetOverTime_Fall)
{
    auto comp = make_asym_slew_rate(6.0f, 3.0f);

    auto st = make_state(10.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 10

    st.values[0] = 0.0f;

    // Need extra frames for one-frame delay
    for (int i = 0; i < 242; ++i) {  // ~4 seconds at 60Hz (slower rate)
        step(comp, st, 1.0f / 60.0f);
    }

    // Should reach target (using rate_down = 3)
    EXPECT_NEAR(st.values[1], 0.0f, 0.2f);
}

TEST(AsymSlewRateTest, VariableDt_AdaptsStepSize)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    st.values[0] = 10.0f;

    // Small dt = small step
    step(comp, st, 0.001f);   // stages small step
    step(comp, st, 0.001f);   // outputs first small step
    float out_small_dt = st.values[1];

    // Large dt = large step (fresh component)
    auto comp2 = make_asym_slew_rate(10.0f, 5.0f);
    auto st2 = make_state(0.0f);
    step(comp2, st2, 1.0f / 60.0f);
    st2.values[0] = 10.0f;
    step(comp2, st2, 0.1f);   // stages large step
    step(comp2, st2, 0.1f);   // outputs first large step
    float out_large_dt = st2.values[1];

    // Larger dt should result in larger change
    EXPECT_GT(out_large_dt, out_small_dt);
}

TEST(AsymSlewRateTest, NegativeInput_Rise)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    st.values[0] = -10.0f;
    step(comp, st, 1.0f / 60.0f);  // stages negative step
    step(comp, st, 1.0f / 60.0f);  // outputs first negative step

    // Should move towards -10.0
    EXPECT_LT(st.values[1], 0.0f);
    EXPECT_GT(st.values[1], -1.0f);  // Limited by rate
}

TEST(AsymSlewRateTest, NegativeInput_Fall)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(-10.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at -10

    st.values[0] = 0.0f;
    step(comp, st, 1.0f / 60.0f);  // stages positive step
    step(comp, st, 1.0f / 60.0f);  // outputs first positive step

    // Should move towards 0 (rate_down = 5, slower)
    EXPECT_LT(st.values[1], -8.0f);  // Barely moved (rate_down = 5)
}

TEST(AsymSlewRateTest, ConstantInput_OutputStaysConstant)
{
    auto comp = make_asym_slew_rate(10.0f, 5.0f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 5

    float initial_out = st.values[1];

    // Keep input constant
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Output should remain constant (in deadzone)
    EXPECT_FLOAT_EQ(st.values[1], initial_out);
}

TEST(AsymSlewRateTest, OscillatingInput_FollowsAsymmetricRates)
{
    // Test with alternating input
    auto comp = make_asym_slew_rate(60.0f, 5.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0f / 60.0f);  // cold start at 0

    // Rise to 10 (rate_up=60 → 1.0/frame, reaches 10 in ~10 frames + 1 delay)
    st.values[0] = 10.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.values[1], 10.0f, 0.2f);

    // Fall to 0 (slower: rate_down=5 → 5.0 units/sec × 1.0s = 5.0 traveled)
    st.values[0] = 0.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }
    // Should only be halfway down (rate_down=5: 5 units/sec × 1s = 5 units of 10)
    EXPECT_NEAR(st.values[1], 5.0f, 0.5f);
}
