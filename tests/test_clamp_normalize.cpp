#include <gtest/gtest.h>
#include <algorithm>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static Clamp<JitProvider> make_clamp(float min_val = 0.0f, float max_val = 1.0f)
{
    Clamp<JitProvider> comp;
    comp.min = min_val;
    comp.max = max_val;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    return comp;
}

static Normalize<JitProvider> make_normalize(float min_val = 0.0f, float max_val = 100.0f)
{
    Normalize<JitProvider> comp;
    comp.min = min_val;
    comp.max = max_val;
    comp.pre_load();
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
// Clamp Tests
// =============================================================================

TEST(ClampTest, WithinRange_PassesThrough)
{
    auto comp = make_clamp(0.0f, 10.0f);
    auto st = make_state(5.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 5.0f);
}

TEST(ClampTest, BelowMinimum_ClampsToMin)
{
    auto comp = make_clamp(0.0f, 10.0f);
    auto st = make_state(-5.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(ClampTest, AboveMaximum_ClampsToMax)
{
    auto comp = make_clamp(0.0f, 10.0f);
    auto st = make_state(15.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 10.0f);
}

TEST(ClampTest, AtBoundary_Min)
{
    auto comp = make_clamp(0.0f, 10.0f);
    auto st = make_state(0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(ClampTest, AtBoundary_Max)
{
    auto comp = make_clamp(0.0f, 10.0f);
    auto st = make_state(10.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 10.0f);
}

TEST(ClampTest, NegativeRange)
{
    auto comp = make_clamp(-10.0f, -5.0f);
    auto st = make_state(-7.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], -7.0f);
}

TEST(ClampTest, NegativeRange_ClampsBelow)
{
    auto comp = make_clamp(-10.0f, -5.0f);
    auto st = make_state(-15.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], -10.0f);
}

TEST(ClampTest, NegativeRange_ClampsAbove)
{
    auto comp = make_clamp(-10.0f, -5.0f);
    auto st = make_state(0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], -5.0f);
}

TEST(ClampTest, ZeroRange_ClampsToSingleValue)
{
    auto comp = make_clamp(5.0f, 5.0f);
    auto st = make_state(100.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 5.0f);
}

TEST(ClampTest, SymmetricRange_Positive)
{
    auto comp = make_clamp(-1.0f, 1.0f);
    auto st = make_state(0.5f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(ClampTest, SymmetricRange_Negative)
{
    auto comp = make_clamp(-1.0f, 1.0f);
    auto st = make_state(-0.5f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], -0.5f);
}

TEST(ClampTest, LargeValues)
{
    auto comp = make_clamp(1000.0f, 10000.0f);
    auto st = make_state(5000.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 5000.0f);
}

// =============================================================================
// Normalize Tests
// =============================================================================

TEST(NormalizeTest, MidRange_MapsToZeroPointFive)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(50.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(NormalizeTest, AtMin_MapsToZero)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(NormalizeTest, AtMax_MapsToOne)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(100.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(NormalizeTest, BelowMin_ClampsToZero)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(-10.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(NormalizeTest, AboveMax_ClampsToOne)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(150.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(NormalizeTest, OffsetRange)
{
    auto comp = make_normalize(20.0f, 120.0f);
    auto st = make_state(70.0f);  // Midpoint

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(NormalizeTest, NegativeRange)
{
    auto comp = make_normalize(-50.0f, 50.0f);
    auto st = make_state(0.0f);  // Midpoint

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(NormalizeTest, NegativeRange_Min)
{
    auto comp = make_normalize(-50.0f, 50.0f);
    auto st = make_state(-50.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(NormalizeTest, NegativeRange_Max)
{
    auto comp = make_normalize(-50.0f, 50.0f);
    auto st = make_state(50.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f);
}

TEST(NormalizeTest, SmallRange_PressureSensor)
{
    // Oil pressure: 0..10 kg/cm²
    auto comp = make_normalize(0.0f, 10.0f);
    auto st = make_state(7.5f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.75f);
}

TEST(NormalizeTest, TemperatureRange_Celsius)
{
    // Temperature: -50..+150°C
    auto comp = make_normalize(-50.0f, 150.0f);
    auto st = make_state(50.0f);  // 1/2 of range

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(NormalizeTest, VoltageRange_Aircraft)
{
    // Aircraft voltage: 20..30V
    auto comp = make_normalize(20.0f, 30.0f);
    auto st = make_state(28.5f);  // Nominal

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.85f, 0.01f);
}

TEST(NormalizeTest, ZeroRange_DefaultsToZero)
{
    // Degenerate case: min == max
    auto comp = make_normalize(50.0f, 50.0f);
    auto st = make_state(100.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    // Should clamp to 0 since inv_range = 0
    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}

TEST(NormalizeTest, PreLoad_ComputesInvRange)
{
    Normalize<JitProvider> comp;
    comp.min = 0.0f;
    comp.max = 100.0f;

    comp.pre_load();

    EXPECT_NEAR(comp.inv_range, 0.01f, 1e-6f);
}

TEST(NormalizeTest, PreLoad_ZeroRange_HandlesGracefully)
{
    Normalize<JitProvider> comp;
    comp.min = 50.0f;
    comp.max = 50.0f;

    comp.pre_load();

    EXPECT_FLOAT_EQ(comp.inv_range, 0.0f);
}

TEST(NormalizeTest, PreLoad_NegativeRange_ComputesCorrectly)
{
    Normalize<JitProvider> comp;
    comp.min = -100.0f;
    comp.max = 100.0f;

    comp.pre_load();

    EXPECT_NEAR(comp.inv_range, 0.005f, 1e-6f);
}

TEST(NormalizeTest, RealWorld_OilPressureWarning)
{
    // Oil pressure < 2 kg/cm² triggers warning
    // Map 0..10 to 0..1, then threshold at 0.2
    auto comp = make_normalize(0.0f, 10.0f);
    auto st = make_state(1.5f);  // Low pressure

    comp.solve_logical(st, 1.0f / 60.0f);

    // Should be 0.15 (below 0.2 threshold)
    EXPECT_NEAR(st.across[1], 0.15f, 0.01f);
}

TEST(NormalizeTest, RealWorld_FuelQuantityGauge)
{
    // Fuel: 0..500 liters
    auto comp = make_normalize(0.0f, 500.0f);
    auto st = make_state(350.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.7f);
}

TEST(NormalizeTest, VariableInput_UpdatesCorrectly)
{
    auto comp = make_normalize(0.0f, 100.0f);
    auto st = make_state(25.0f);

    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.25f);

    st.across[0] = 75.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[1], 0.75f);
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(ClampTest, Regression_InvertedRange_NoCrash)
{
    // std::clamp has UB when min > max.
    // After factory fix (swap), inverted params behave as normal range.
    // Direct unit test: manually swap to simulate the factory fix.
    Clamp<JitProvider> comp;
    comp.min = 10.0f;
    comp.max = 5.0f;
    if (comp.min > comp.max) std::swap(comp.min, comp.max);
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);

    auto st = make_state(7.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[1], 7.0f);

    auto st2 = make_state(3.0f);
    comp.solve_logical(st2, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st2.across[1], 5.0f);

    auto st3 = make_state(12.0f);
    comp.solve_logical(st3, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st3.across[1], 10.0f);
}

TEST(NormalizeTest, Regression_InvertedRange_SafeOutput)
{
    // min > max: range is negative, inv_range is negative.
    // Output is clamped to [0,1] so no crash, but behaviour reverses.
    auto comp = make_normalize(100.0f, 0.0f);
    auto st = make_state(50.0f);  // midpoint stays 0.5

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(NormalizeTest, Regression_VerySmallRange)
{
    // Range just above epsilon guard (1e-6)
    auto comp = make_normalize(0.0f, 1e-5f);
    auto st = make_state(5e-6f);  // midpoint

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f);
}

TEST(ClampTest, Regression_ZeroInput)
{
    auto comp = make_clamp(-10.0f, 10.0f);
    auto st = make_state(0.0f);

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f);
}
