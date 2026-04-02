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
// SlewRate Tests
// =============================================================================

TEST(SlewRateTest, ColdStart_FirstFrame)
{
    auto comp = make_slew_rate();
    auto st = make_state(10.0f);

    // Cold start: output = committed value (cold-start-adjusted to input)
    // After commit, first_frame_mask is consumed (0.0)
    step(comp, st, 1.0 / 60.0);

    // One-frame delay: output is the cold-start-adjusted committed value = 10.0
    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(SlewRateTest, LimitsRiseRate)
{
    // max_rate = 10 units/sec, dt = 1/60 sec
    // Max step per frame = 10 * (1/60) = 0.1667
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 0.0

    // Step to 10.0 (requires 10 units/sec rise)
    st.values[0] = 10.0f;
    step(comp, st, 1.0 / 60.0);  // output = committed 0.0, stages 0.167
    // One-frame delay: output still shows committed value from before this step
    EXPECT_NEAR(st.values[1], 0.0f, 0.001f);

    // Next frame: committed is now 0.167, output shows 0.167
    step(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 0.167f, 0.001f);
}

TEST(SlewRateTest, LimitsFallRate)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(10.0f);
    step(comp, st, 1.0 / 60.0);  // cold start at 10.0

    // Step to 0.0 (requires 10 units/sec fall)
    st.values[0] = 0.0f;
    step(comp, st, 1.0 / 60.0);  // output = committed 10.0, stages 9.833
    EXPECT_NEAR(st.values[1], 10.0f, 0.001f);  // one-frame delay

    // Next frame: committed is now 9.833
    step(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 10.0f - 0.167f, 0.001f);
}

TEST(SlewRateTest, AsymmetricLimits_SameRate)
{
    // Same max_rate for rise and fall
    auto comp = make_slew_rate(5.0f);

    // Test rise
    auto st_rise = make_state(0.0f);
    step(comp, st_rise, 1.0f / 60.0f);
    st_rise.values[0] = 10.0f;
    step(comp, st_rise, 1.0f / 60.0f);  // stages rise step
    step(comp, st_rise, 1.0f / 60.0f);  // outputs first rise step
    float rise_change = st_rise.values[1];

    // Test fall
    auto comp_fall = make_slew_rate(5.0f);
    auto st_fall = make_state(10.0f);
    step(comp_fall, st_fall, 1.0f / 60.0f);
    st_fall.values[0] = 0.0f;
    step(comp_fall, st_fall, 1.0f / 60.0f);  // stages fall step
    step(comp_fall, st_fall, 1.0f / 60.0f);  // outputs first fall step
    float fall_change = 10.0f - st_fall.values[1];

    // Rise and fall rates should be equal
    EXPECT_NEAR(rise_change, fall_change, 0.001f);
}

TEST(SlewRateTest, ApproachesTargetOverTime)
{
    auto comp = make_slew_rate(6.0f);  // 6 units/sec = 0.1 units/frame at 60Hz

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 10.0f;

    // With one-frame delay, need extra frames to fully converge
    for (int i = 0; i < 102; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // After 102 frames (~1.7 seconds at 6 units/sec),
    // should have moved at most 6 * 1.7 = 10.2 units
    EXPECT_NEAR(st.values[1], 10.0f, 0.2f);
}

TEST(SlewRateTest, HandlesZeroDt_Pause)
{
    auto comp = make_slew_rate();

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    float out_before_pause = st.values[1];

    // Simulate pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    // Output should not change during pause
    EXPECT_FLOAT_EQ(st.values[1], out_before_pause);
}

TEST(SlewRateTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_slew_rate(10.0f, 0.5f);  // Large deadzone

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    float initial_out = st.values[1];

    // Change input by less than deadzone
    st.values[0] = 5.3f;  // diff = 0.3 < deadzone (0.5)

    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should not have changed
    EXPECT_NEAR(st.values[1], initial_out, 0.001f);
}

TEST(SlewRateTest, Deadzone_AllowsLargeChanges)
{
    auto comp = make_slew_rate(10.0f, 0.5f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    // Change input by MORE than deadzone
    st.values[0] = 10.0f;  // diff = 5.0 > deadzone (0.5)

    for (int i = 0; i < 12; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should approach new input (with one-frame delay, needs extra frames)
    EXPECT_GT(st.values[1], 6.0f);
}

TEST(SlewRateTest, ZeroRate_NoChange)
{
    auto comp = make_slew_rate(0.0f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    float initial_out = st.values[1];

    st.values[0] = 10.0f;

    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // With zero rate, output should not change
    EXPECT_NEAR(st.values[1], initial_out, 0.1f);
}

TEST(SlewRateTest, InfiniteRate_InstantTracking)
{
    // Very large rate means essentially no limit
    auto comp = make_slew_rate(100000.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 10.0f;
    step(comp, st, 1.0 / 60.0);  // output = committed 0.0, stages ~10.0
    // One-frame delay: output still shows 0.0
    step(comp, st, 1.0 / 60.0);  // output = committed ~10.0

    // With very large max_rate, should reach target after one-frame delay
    EXPECT_NEAR(st.values[1], 10.0f, 0.5f);
}

TEST(SlewRateTest, PreservesStateBetweenFrames)
{
    auto comp = make_slew_rate(6.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 10.0f;
    step(comp, st, 1.0 / 60.0);  // output = 0.0 (committed), stages 0.1
    step(comp, st, 1.0 / 60.0);  // output = 0.1 (committed), stages 0.2
    float out1 = st.values[1];

    // Same input, next frame should continue approaching target
    step(comp, st, 1.0 / 60.0);
    float out2 = st.values[1];

    EXPECT_GT(out2, out1);
    EXPECT_LT(out2, 10.0f);
}

TEST(SlewRateTest, VariableDt_AdaptsStepSize)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 10.0f;

    // Small dt = small step
    step(comp, st, 0.001f);   // stages small step
    step(comp, st, 0.001f);   // output shows first small step
    float out_small_dt = st.values[1];

    // Large dt = large step (fresh component)
    auto comp2 = make_slew_rate(10.0f);
    auto st2 = make_state(0.0f);
    step(comp2, st2, 1.0f / 60.0f);
    st2.values[0] = 10.0f;
    step(comp2, st2, 0.1f);   // stages large step
    step(comp2, st2, 0.1f);   // output shows first large step
    float out_large_dt = st2.values[1];

    // Larger dt should result in larger change
    EXPECT_GT(out_large_dt, out_small_dt);
}

TEST(SlewRateTest, NegativeInput_HandlesCorrectly)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = -10.0f;
    step(comp, st, 1.0 / 60.0);  // stages negative step
    step(comp, st, 1.0 / 60.0);  // outputs first negative step

    // Should fall towards -10.0
    EXPECT_LT(st.values[1], 0.0f);
    EXPECT_GT(st.values[1], -1.0f);  // Limited by rate
}

TEST(SlewRateTest, CrossingZero_WorksCorrectly)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(10.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = -10.0f;

    for (int i = 0; i < 62; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should approach -10.0, crossing zero
    EXPECT_LT(st.values[1], 0.0f);
}

TEST(SlewRateTest, ZeroDeadzone_AllowsAllChanges)
{
    auto comp = make_slew_rate(10.0f, 0.0f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    // Even tiny change should propagate
    st.values[0] = 5.001f;

    for (int i = 0; i < 12; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Should approach new value
    EXPECT_GT(st.values[1], 5.0005f);
}

TEST(SlewRateTest, ConstantInput_OutputStaysConstant)
{
    auto comp = make_slew_rate(10.0f);

    auto st = make_state(5.0f);
    step(comp, st, 1.0 / 60.0);

    float initial_out = st.values[1];

    // Keep input constant
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should remain constant (in deadzone)
    EXPECT_FLOAT_EQ(st.values[1], initial_out);
}

TEST(SlewRateTest, StepChange_CorrectTotalTime)
{
    // At 60 Hz with max_rate = 60 units/sec:
    // To go from 0 to 60 should take exactly 1 second (60 frames)
    // With one-frame delay, need 61 frames
    auto comp = make_slew_rate(60.0f);

    auto st = make_state(0.0f);
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 60.0f;

    for (int i = 0; i < 62; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // After 62 frames at 60 Hz with 60 units/sec rate (accounting for one-frame delay),
    // should be very close to target
    EXPECT_NEAR(st.values[1], 60.0f, 0.5f);
}
