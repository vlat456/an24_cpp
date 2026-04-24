#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static SampleHold<JitProvider> make_sample_hold()
{
    SampleHold<JitProvider> comp;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::trigger, 1);
    comp.provider.set(PortNames::out, 2);
    return comp;
}

static SimulationState make_state(float input_val, float trigger_val)
{
    SimulationState st;
    st.values.resize(3, 0.0f);
    st.values[0] = input_val;
    st.values[1] = trigger_val;
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
// SampleHold Tests
// =============================================================================

TEST(SampleHoldTest, InitiallyZero)
{
    auto comp = make_sample_hold();
    auto st = make_state(5.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Initially no trigger, so output should be 0 (committed stored_value = 0)
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SampleHoldTest, RisingEdge_SamplesInput)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Rising edge on trigger
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    // Output = committed stored_value = 0 (one-frame delay)
    // But stored_value is now staged → committed as 42.0

    // Next frame: output = committed stored_value = 42.0
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 42.0f);
    EXPECT_FLOAT_EQ(comp.stored_value, 42.0f);
}

TEST(SampleHoldTest, NoTrigger_HoldsValue)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample via rising edge
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages stored_value = 10.0
    step(comp, st, 1.0 / 60.0);  // output = 10.0

    float sampled_value = st.values[2];
    EXPECT_FLOAT_EQ(sampled_value, 10.0f);

    // Change input but keep trigger high (no new rising edge)
    st.values[0] = 999.0f;
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Output should not have changed
    EXPECT_FLOAT_EQ(st.values[2], sampled_value);
}

TEST(SampleHoldTest, HighTrigger_DoesNotResample)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 10.0
    step(comp, st, 1.0 / 60.0);  // output = 10.0
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Keep trigger high, change input
    st.values[0] = 20.0f;
    step(comp, st, 1.0 / 60.0);

    // Should not resample (no rising edge)
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);
}

TEST(SampleHoldTest, FallingEdge_DoesNotResample)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Trigger goes high (this samples 10.0)
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Trigger goes low (should NOT resample)
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Change input
    st.values[0] = 20.0f;
    step(comp, st, 1.0 / 60.0);

    // Should still be 10.0 (not resampled on falling edge)
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);
}

TEST(SampleHoldTest, MultipleTriggers_ResamplesEachTime)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // First sample
    st.values[0] = 10.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 10.0
    step(comp, st, 1.0 / 60.0);  // output = 10.0
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Second sample (need falling edge first)
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[0] = 20.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 20.0
    step(comp, st, 1.0 / 60.0);  // output = 20.0
    EXPECT_FLOAT_EQ(st.values[2], 20.0f);

    // Third sample
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[0] = 30.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 30.0
    step(comp, st, 1.0 / 60.0);  // output = 30.0
    EXPECT_FLOAT_EQ(st.values[2], 30.0f);
}

TEST(SampleHoldTest, NegativeInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(-42.5f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages -42.5
    step(comp, st, 1.0 / 60.0);  // output = -42.5

    EXPECT_FLOAT_EQ(st.values[2], -42.5f);
}

TEST(SampleHoldTest, ZeroInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SampleHoldTest, LargeInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(99999.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 99999
    step(comp, st, 1.0 / 60.0);  // output = 99999

    EXPECT_FLOAT_EQ(st.values[2], 99999.0f);
}

TEST(SampleHoldTest, BooleanThreshold_Trigger)
{
    auto comp = make_sample_hold();
    auto st = make_state(50.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Below threshold (0.4 <= 0.5, not a rising edge from 0)
    st.values[1] = 0.4f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(comp.stored_value, 0.0f);

    // Rising edge through threshold (0.4 → 0.6)
    st.values[1] = 0.6f;
    step(comp, st, 1.0 / 60.0);  // stages 50.0
    step(comp, st, 1.0 / 60.0);  // output = 50.0
    EXPECT_FLOAT_EQ(st.values[2], 50.0f);
}

TEST(SampleHoldTest, ContinuousTrigger_SamplesOnlyOnEdge)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // First trigger
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Keep trigger high, change input multiple times — no re-sample
    st.values[0] = 20.0f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    st.values[0] = 30.0f;
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);
}

TEST(SampleHoldTest, RapidTriggering_ResamplesOnEachEdge)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Rapid toggle: each rising edge stages a new sample
    for (int i = 0; i < 10; ++i) {
        st.values[0] = static_cast<float>(i * 10);
        st.values[1] = 1.0f;
        step(comp, st, 1.0 / 60.0);

        st.values[1] = 0.0f;
        step(comp, st, 1.0 / 60.0);
    }

    // After the loop, committed stored_value = last sampled = 90.0
    // But output has one-frame delay, so we need one more frame to see it
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 90.0f);
}

TEST(SampleHoldTest, VariableDt_NoEffectOnSampling)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample with different dt
    st.values[1] = 1.0f;
    step(comp, st, 0.001f);
    step(comp, st, 0.001f);
    EXPECT_FLOAT_EQ(st.values[2], 42.0f);

    st.values[1] = 0.0f;
    step(comp, st, 0.001f);

    st.values[0] = 84.0f;
    st.values[1] = 1.0f;
    step(comp, st, 0.1f);
    step(comp, st, 0.1f);
    EXPECT_FLOAT_EQ(st.values[2], 84.0f);
}

TEST(SampleHoldTest, IndependentOfDt)
{
    auto comp = make_sample_hold();
    auto st = make_state(123.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 123.0f);

    // Hold for various dt values (trigger stays high, no new edge)
    step(comp, st, 0.0f);
    EXPECT_FLOAT_EQ(st.values[2], 123.0f);

    step(comp, st, 0.001f);
    EXPECT_FLOAT_EQ(st.values[2], 123.0f);

    step(comp, st, 0.1f);
    EXPECT_FLOAT_EQ(st.values[2], 123.0f);

    step(comp, st, 1.0);
    EXPECT_FLOAT_EQ(st.values[2], 123.0f);
}

TEST(SampleHoldTest, PressureCapture_RealisticUseCase)
{
    // Simulate capturing oil pressure at engine startup
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Engine builds up pressure
    st.values[0] = 3.5f;  // 3.5 bar
    step(comp, st, 1.0 / 60.0);

    // Capture pressure when engine reaches idle (trigger)
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);  // stages 3.5
    step(comp, st, 1.0 / 60.0);  // output = 3.5

    EXPECT_FLOAT_EQ(st.values[2], 3.5f);

    // Pressure changes later, but captured value stays
    st.values[0] = 4.2f;
    for (int i = 0; i < 100; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_FLOAT_EQ(st.values[2], 3.5f);  // Still 3.5
}

TEST(SampleHoldTest, MaxValueCapture_WithComparator)
{
    // Simulate capturing max value (triggered by comparator)
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Values over time
    float values[] = {10.0f, 20.0f, 15.0f, 25.0f, 18.0f};

    // Sample on each "new max" (simulated by manual trigger)
    float max_val = 0.0f;
    for (float v : values) {
        st.values[0] = v;
        if (v > max_val) {
            max_val = v;
            st.values[1] = 1.0f;
            step(comp, st, 1.0 / 60.0);
            st.values[1] = 0.0f;
            step(comp, st, 1.0 / 60.0);
        }
    }

    // Should have captured maximum value (one-frame delay means we need one more step)
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 25.0f);
}

TEST(SampleHoldTest, TriggerAtZero_DoesNotSample)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // No rising edge (stays at 0)
    for (int i = 0; i < 10; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SampleHoldTest, TriggerStaysHigh_NoMoreSamples)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Rising edge
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Keep high, change input — no re-sample
    st.values[0] = 999.0f;
    for (int i = 0; i < 100; ++i) {
        step(comp, st, 1.0 / 60.0);
    }

    // Still 10.0
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);
}

TEST(SampleHoldTest, InputPrecision_Maintained)
{
    auto comp = make_sample_hold();
    auto st = make_state(3.14159265f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    step(comp, st, 1.0 / 60.0);

    // Should maintain precision
    EXPECT_NEAR(st.values[2], 3.14159265f, 0.00001f);
}

TEST(SampleHoldTest, SequentialSamples_LastOneWins)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    step(comp, st, 1.0 / 60.0);

    // Sample sequence
    st.values[0] = 1.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 2.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    st.values[0] = 3.0f;
    st.values[1] = 1.0f;
    step(comp, st, 1.0 / 60.0);
    st.values[1] = 0.0f;
    step(comp, st, 1.0 / 60.0);

    // Last sample wins (one-frame delay → need one more step)
    step(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 3.0f);
}
