#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

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
    st.across.resize(3, 0.0f);
    st.through.resize(3, 0.0f);
    st.conductance.resize(3, 0.0f);
    st.across[0] = input_val;
    st.across[1] = trigger_val;
    st.across[2] = 0.0f;
    return st;
}

// =============================================================================
// SampleHold Tests
// =============================================================================

TEST(SampleHoldTest, InitiallyZero)
{
    auto comp = make_sample_hold();
    auto st = make_state(5.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Initially no trigger, so output should be 0
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SampleHoldTest, RisingEdge_SamplesInput)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Rising edge on trigger
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should have sampled input value
    EXPECT_FLOAT_EQ(st.across[2], 42.0f);
    EXPECT_FLOAT_EQ(comp.stored_value, 42.0f);
}

TEST(SampleHoldTest, NoTrigger_HoldsValue)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    float sampled_value = st.across[2];
    EXPECT_FLOAT_EQ(sampled_value, 10.0f);

    // Change input but keep trigger low
    st.across[0] = 999.0f;
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Output should not have changed
    EXPECT_FLOAT_EQ(st.across[2], sampled_value);
}

TEST(SampleHoldTest, HighTrigger_DoesNotResample)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    // Keep trigger high, change input
    st.across[0] = 20.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should not resample
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(SampleHoldTest, FallingEdge_DoesNotResample)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);  // Start with trigger low

    comp.solve_logical(st, 1.0f / 60.0f);

    // Trigger goes high (this samples 10.0)
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    // Trigger goes low (should NOT resample)
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Change input
    st.across[0] = 20.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should still be 10.0 (not resampled on falling edge)
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(SampleHoldTest, MultipleTriggers_ResamplesEachTime)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First sample
    st.across[0] = 10.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    // Second sample (need falling edge first)
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[0] = 20.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 20.0f);

    // Third sample
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[0] = 30.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 30.0f);
}

TEST(SampleHoldTest, NegativeInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(-42.5f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], -42.5f);
}

TEST(SampleHoldTest, ZeroInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SampleHoldTest, LargeInput_SamplesCorrectly)
{
    auto comp = make_sample_hold();
    auto st = make_state(99999.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 99999.0f);
}

TEST(SampleHoldTest, BooleanThreshold_Trigger)
{
    auto comp = make_sample_hold();
    auto st = make_state(50.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Below threshold
    st.across[1] = 0.4f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(comp.stored_value, 0.0f);

    // Rising edge through threshold
    st.across[1] = 0.6f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 50.0f);
}

TEST(SampleHoldTest, ContinuousTrigger_SamplesOnlyOnEdge)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // First trigger
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    // Keep trigger high, change input multiple times
    st.across[0] = 20.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    st.across[0] = 30.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(SampleHoldTest, RapidTriggering_ResamplesOnEachEdge)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Rapid toggle
    for (int i = 0; i < 10; ++i) {
        st.across[0] = static_cast<float>(i * 10);
        st.across[1] = 1.0f;
        comp.solve_logical(st, 1.0f / 60.0f);

        st.across[1] = 0.0f;
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Should have sampled last value (90.0)
    EXPECT_FLOAT_EQ(st.across[2], 90.0f);
}

TEST(SampleHoldTest, VariableDt_NoEffectOnSampling)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample with different dt
    st.across[1] = 1.0f;
    comp.solve_logical(st, 0.001f);
    EXPECT_FLOAT_EQ(st.across[2], 42.0f);

    st.across[1] = 0.0f;
    comp.solve_logical(st, 0.001f);

    st.across[0] = 84.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 0.1f);
    EXPECT_FLOAT_EQ(st.across[2], 84.0f);
}

TEST(SampleHoldTest, IndependentOfDt)
{
    auto comp = make_sample_hold();
    auto st = make_state(123.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Hold for various dt values
    comp.solve_logical(st, 0.0f);
    EXPECT_FLOAT_EQ(st.across[2], 123.0f);

    comp.solve_logical(st, 0.001f);
    EXPECT_FLOAT_EQ(st.across[2], 123.0f);

    comp.solve_logical(st, 0.1f);
    EXPECT_FLOAT_EQ(st.across[2], 123.0f);

    comp.solve_logical(st, 1.0f);
    EXPECT_FLOAT_EQ(st.across[2], 123.0f);
}

TEST(SampleHoldTest, PressureCapture_RealisticUseCase)
{
    // Simulate capturing oil pressure at engine startup
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Engine builds up pressure
    st.across[0] = 3.5f;  // 3.5 bar
    comp.solve_logical(st, 1.0f / 60.0f);

    // Capture pressure when engine reaches idle (trigger)
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 3.5f);

    // Pressure changes later, but captured value stays
    st.across[0] = 4.2f;
    for (int i = 0; i < 100; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(st.across[2], 3.5f);  // Still 3.5
}

TEST(SampleHoldTest, MaxValueCapture_WithComparator)
{
    // Simulate capturing max value (triggered by comparator)
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Values over time
    float values[] = {10.0f, 20.0f, 15.0f, 25.0f, 18.0f};

    // Sample on each "new max" (simulated by manual trigger)
    float max_val = 0.0f;
    for (float v : values) {
        st.across[0] = v;
        if (v > max_val) {
            max_val = v;
            st.across[1] = 1.0f;
            comp.solve_logical(st, 1.0f / 60.0f);
            st.across[1] = 0.0f;
            comp.solve_logical(st, 1.0f / 60.0f);
        }
    }

    // Should have captured maximum value
    EXPECT_FLOAT_EQ(st.across[2], 25.0f);
}

TEST(SampleHoldTest, TriggerAtZero_DoesNotSample)
{
    auto comp = make_sample_hold();
    auto st = make_state(42.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // No rising edge (stays at 0)
    for (int i = 0; i < 10; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SampleHoldTest, TriggerStaysHigh_NoMoreSamples)
{
    auto comp = make_sample_hold();
    auto st = make_state(10.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Rising edge
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);

    // Keep high, change input
    st.across[0] = 999.0f;
    for (int i = 0; i < 100; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }

    // Still 10.0
    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(SampleHoldTest, InputPrecision_Maintained)
{
    auto comp = make_sample_hold();
    auto st = make_state(3.14159265f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Should maintain precision
    EXPECT_NEAR(st.across[2], 3.14159265f, 0.00001f);
}

TEST(SampleHoldTest, SequentialSamples_LastOneWins)
{
    auto comp = make_sample_hold();
    auto st = make_state(0.0f, 0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Sample sequence
    st.across[0] = 1.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 2.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    st.across[0] = 3.0f;
    st.across[1] = 1.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    st.across[1] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);

    // Last sample wins
    EXPECT_FLOAT_EQ(st.across[2], 3.0f);
}
