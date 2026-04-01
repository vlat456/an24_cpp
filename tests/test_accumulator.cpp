#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static Accumulator<JitProvider> make_accumulator(float initial_val = 0.0f)
{
    Accumulator<JitProvider> comp;
    comp.initial_val = initial_val;
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
    comp.commit(st, dt);
}

#define step step_component

// =============================================================================
// Accumulator Tests
// =============================================================================

TEST(AccumulatorTest, ColdStart_StartsAtInitialValue)
{
    auto comp = make_accumulator(5.0f);
    auto st = make_state(0.0f);

    step(comp, st, 1.0f / 60.0f);

    // Cold start: output = committed state (cold-start-adjusted to initial_val = 5.0)
    EXPECT_FLOAT_EQ(st.values[1], 5.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
    // After commit, state = initial_val + 0 = 5.0
    EXPECT_FLOAT_EQ(comp.state, 5.0f);
}

TEST(AccumulatorTest, ColdStart_ZeroInitial)
{
    auto comp = make_accumulator(0.0f);
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);

    // Cold start: output = 0.0 (committed state at initial_val)
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(AccumulatorTest, Accumulation_PositiveInput)
{
    auto comp = make_accumulator(0.0f);
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start, output = 0.0

    // Accumulate for 1 second at 10 units/sec (60 frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Should be approximately 10.0 (10 * 1.0)
    EXPECT_NEAR(st.values[1], 10.0f, 0.2f);
}

TEST(AccumulatorTest, Accumulation_NegativeInput)
{
    auto comp = make_accumulator(100.0f);
    auto st = make_state(-5.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start, output = 100.0

    // Accumulate for 2 seconds at -5 units/sec (120 frames)
    for (int i = 0; i < 120; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Should be approximately 90.0 (100 + (-5 * 2.0))
    EXPECT_NEAR(st.values[1], 90.0f, 0.2f);
}

TEST(AccumulatorTest, ZeroInput_NoAccumulation)
{
    auto comp = make_accumulator(5.0f);
    auto st = make_state(0.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start

    // Accumulate zero
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Should stay at initial value
    EXPECT_FLOAT_EQ(st.values[1], 5.0f);
}

TEST(AccumulatorTest, ZeroDt_NoAccumulation)
{
    auto comp = make_accumulator(5.0f);
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start

    // Set input to 0 to stop accumulation, then flush pipeline
    st.values[0] = 0.0f;
    step(comp, st, 0.0f);
    step(comp, st, 0.0f);

    float before = st.values[1];

    // Pause (dt=0 should not accumulate)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 0.0f);
    }

    EXPECT_FLOAT_EQ(st.values[1], before);
}

TEST(AccumulatorTest, LargeDt)
{
    auto comp = make_accumulator(0.0f);
    auto st = make_state(1.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start, output = 0

    // Very large dt
    step(comp, st, 10.0f);
    // Output = committed state (≈ 1/60 from first post-cold-start frame)
    // After commit, state ≈ 1/60 + 1.0 * 10.0 = 10.0167

    // Next frame shows committed value
    step(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[1], 10.0f, 0.5f);
}

TEST(AccumulatorTest, NoGainParameter)
{
    // Accumulator has no gain - unlike Integrator.
    // Input directly accumulates: state += in * dt
    auto comp = make_accumulator(0.0f);
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start

    // 60 frames at 10 units/sec = 10 accumulated
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    EXPECT_NEAR(st.values[1], 10.0f, 0.2f);
}

TEST(AccumulatorTest, NegativeCrossingZero)
{
    auto comp = make_accumulator(50.0f);
    auto st = make_state(-10.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start, output = 50

    // Accumulate for 6 seconds (360 frames)
    for (int i = 0; i < 360; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Should cross zero and go negative (50 + (-10)*6 = -10)
    EXPECT_LT(st.values[1], 0.0f);
    EXPECT_NEAR(st.values[1], -10.0f, 0.5f);
}

TEST(AccumulatorTest, AlternatingInput)
{
    auto comp = make_accumulator(0.0f);
    auto st = make_state(10.0f);

    step(comp, st, 1.0f / 60.0f);  // cold start

    // Positive for 1 second (60 frames)
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.values[1], 10.0f, 0.5f);

    // Negative for 1 second
    st.values[0] = -10.0f;
    for (int i = 0; i < 60; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.values[1], 0.0f, 0.5f);
}

TEST(AccumulatorTest, Precision_MaintainedOverTime)
{
    auto comp = make_accumulator(0.0f);
    auto st = make_state(1.0f);

    step(comp, st, 1.0f / 60.0f);

    // Accumulate for 60 seconds (3600 frames)
    for (int i = 0; i < 3600; ++i) {
        step(comp, st, 1.0f / 60.0f);
    }

    // Should be 60.0 (1.0 * 60)
    EXPECT_NEAR(st.values[1], 60.0f, 0.5f);
}
