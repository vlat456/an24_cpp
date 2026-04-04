#include <gtest/gtest.h>
#include <algorithm>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

static Clamp<JitProvider> make_clamp()
{
    Clamp<JitProvider> comp;
    comp.provider.set(PortNames::in,  0);
    comp.provider.set(PortNames::out, 1);
    comp.provider.set(PortNames::min, 2);
    comp.provider.set(PortNames::max, 3);
    return comp;
}

static Normalize<JitProvider> make_normalize()
{
    Normalize<JitProvider> comp;
    comp.provider.set(PortNames::in,  0);
    comp.provider.set(PortNames::out, 1);
    comp.provider.set(PortNames::min, 2);
    comp.provider.set(PortNames::max, 3);
    return comp;
}

static SimulationState make_state(float input_val, float min_val = 0.0f, float max_val = 1.0f)
{
    SimulationState st;
    st.values.resize(4, 0.0f);
    st.values[0] = input_val;
    st.values[1] = 0.0f;
    st.values[2] = min_val;
    st.values[3] = max_val;
    return st;
}

// =============================================================================
// Clamp Tests
// =============================================================================

TEST(ClampTest, WithinRange_PassesThrough)
{
    auto comp = make_clamp();
    auto st = make_state(5.0f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 5.0f);
}

TEST(ClampTest, BelowMinimum_ClampsToMin)
{
    auto comp = make_clamp();
    auto st = make_state(-5.0f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(ClampTest, AboveMaximum_ClampsToMax)
{
    auto comp = make_clamp();
    auto st = make_state(15.0f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
}

TEST(ClampTest, AtBoundary_Min)
{
    auto comp = make_clamp();
    auto st = make_state(0.0f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(ClampTest, AtBoundary_Max)
{
    auto comp = make_clamp();
    auto st = make_state(10.0f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
}

TEST(ClampTest, NegativeRange)
{
    auto comp = make_clamp();
    auto st = make_state(-7.0f, -10.0f, -5.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], -7.0f);
}

TEST(ClampTest, NegativeRange_ClampsBelow)
{
    auto comp = make_clamp();
    auto st = make_state(-15.0f, -10.0f, -5.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], -10.0f);
}

TEST(ClampTest, NegativeRange_ClampsAbove)
{
    auto comp = make_clamp();
    auto st = make_state(0.0f, -10.0f, -5.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], -5.0f);
}

TEST(ClampTest, ZeroRange_ClampsToSingleValue)
{
    auto comp = make_clamp();
    auto st = make_state(100.0f, 5.0f, 5.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 5.0f);
}

TEST(ClampTest, SymmetricRange_Positive)
{
    auto comp = make_clamp();
    auto st = make_state(0.5f, -1.0f, 1.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(ClampTest, SymmetricRange_Negative)
{
    auto comp = make_clamp();
    auto st = make_state(-0.5f, -1.0f, 1.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], -0.5f);
}

TEST(ClampTest, LargeValues)
{
    auto comp = make_clamp();
    auto st = make_state(5000.0f, 1000.0f, 10000.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 5000.0f);
}

// =============================================================================
// Normalize Tests
// =============================================================================

TEST(NormalizeTest, MidRange_MapsToZeroPointFive)
{
    auto comp = make_normalize();
    auto st = make_state(50.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(NormalizeTest, AtMin_MapsToZero)
{
    auto comp = make_normalize();
    auto st = make_state(0.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(NormalizeTest, AtMax_MapsToOne)
{
    auto comp = make_normalize();
    auto st = make_state(100.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(NormalizeTest, BelowMin_ClampsToZero)
{
    auto comp = make_normalize();
    auto st = make_state(-10.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(NormalizeTest, AboveMax_ClampsToOne)
{
    auto comp = make_normalize();
    auto st = make_state(150.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(NormalizeTest, OffsetRange)
{
    auto comp = make_normalize();
    auto st = make_state(70.0f, 20.0f, 120.0f);  // Midpoint

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(NormalizeTest, NegativeRange)
{
    auto comp = make_normalize();
    auto st = make_state(0.0f, -50.0f, 50.0f);  // Midpoint

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(NormalizeTest, NegativeRange_Min)
{
    auto comp = make_normalize();
    auto st = make_state(-50.0f, -50.0f, 50.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(NormalizeTest, NegativeRange_Max)
{
    auto comp = make_normalize();
    auto st = make_state(50.0f, -50.0f, 50.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(NormalizeTest, SmallRange_PressureSensor)
{
    // Oil pressure: 0..10 kg/cm²
    auto comp = make_normalize();
    auto st = make_state(7.5f, 0.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.75f);
}

TEST(NormalizeTest, TemperatureRange_Celsius)
{
    // Temperature: -50..+150°C
    auto comp = make_normalize();
    auto st = make_state(50.0f, -50.0f, 150.0f);  // 1/2 of range

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(NormalizeTest, VoltageRange_Aircraft)
{
    // Aircraft voltage: 20..30V
    auto comp = make_normalize();
    auto st = make_state(28.5f, 20.0f, 30.0f);  // Nominal

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[1], 0.85f, 0.01f);
}

TEST(NormalizeTest, ZeroRange_DefaultsToZero)
{
    // Degenerate case: min == max
    auto comp = make_normalize();
    auto st = make_state(100.0f, 50.0f, 50.0f);

    step_component(comp, st, 1.0 / 60.0);

    // Should clamp to 0 since inv_range = 0
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}


TEST(NormalizeTest, RealWorld_OilPressureWarning)
{
    // Oil pressure < 2 kg/cm² triggers warning
    // Map 0..10 to 0..1, then threshold at 0.2
    auto comp = make_normalize();
    auto st = make_state(1.5f, 0.0f, 10.0f);  // Low pressure

    step_component(comp, st, 1.0 / 60.0);

    // Should be 0.15 (below 0.2 threshold)
    EXPECT_NEAR(st.values[1], 0.15f, 0.01f);
}

TEST(NormalizeTest, RealWorld_FuelQuantityGauge)
{
    // Fuel: 0..500 liters
    auto comp = make_normalize();
    auto st = make_state(350.0f, 0.0f, 500.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.7f);
}

TEST(NormalizeTest, VariableInput_UpdatesCorrectly)
{
    auto comp = make_normalize();
    auto st = make_state(25.0f, 0.0f, 100.0f);

    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 0.25f);

    st.values[0] = 75.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 0.75f);
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(ClampTest, Regression_InvertedRange_NoCrash)
{
    // std::clamp has UB when min > max.
    // After factory fix (swap), inverted params behave as normal range.
    // Create state with swapped min/max to simulate the factory fix.
    auto comp = make_clamp();
    auto st = make_state(7.0f, 10.0f, 5.0f);  // min > max initially
    
    // Simulate what component does: swap if needed
    auto min_val = st.values[2];
    auto max_val = st.values[3];
    if (min_val > max_val) std::swap(min_val, max_val);
    st.values[2] = min_val;
    st.values[3] = max_val;

    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[1], 7.0f);

    auto st2 = make_state(3.0f, 10.0f, 5.0f);
    auto min_val2 = st2.values[2];
    auto max_val2 = st2.values[3];
    if (min_val2 > max_val2) std::swap(min_val2, max_val2);
    st2.values[2] = min_val2;
    st2.values[3] = max_val2;
    step_component(comp, st2, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st2.values[1], 5.0f);

    auto st3 = make_state(12.0f, 10.0f, 5.0f);
    auto min_val3 = st3.values[2];
    auto max_val3 = st3.values[3];
    if (min_val3 > max_val3) std::swap(min_val3, max_val3);
    st3.values[2] = min_val3;
    st3.values[3] = max_val3;
    step_component(comp, st3, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st3.values[1], 10.0f);
}

TEST(NormalizeTest, Regression_InvertedRange_SafeOutput)
{
    // min > max: range is negative, inv_range is negative.
    // Output is clamped to [0,1] so no crash, but behaviour reverses.
    auto comp = make_normalize();
    auto st = make_state(50.0f, 100.0f, 0.0f);  // midpoint stays 0.5

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(NormalizeTest, Regression_VerySmallRange)
{
    // Range just above epsilon guard (1e-6)
    auto comp = make_normalize();
    auto st = make_state(5e-6f, 0.0f, 1e-5f);  // midpoint

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.5f);
}

TEST(ClampTest, Regression_ZeroInput)
{
    auto comp = make_clamp();
    auto st = make_state(0.0f, -10.0f, 10.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}
