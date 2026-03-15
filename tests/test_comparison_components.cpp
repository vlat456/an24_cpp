#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static SimulationState make_state_2in(float A_val, float B_val)
{
    SimulationState st;
    st.across.resize(3, 0.0f);
    st.through.resize(3, 0.0f);
    st.conductance.resize(3, 0.0f);
    st.across[0] = A_val;
    st.across[1] = B_val;
    st.across[2] = 0.0f;
    return st;
}

// =============================================================================
// Min Tests
// =============================================================================

TEST(MinTest, APicksSmaller)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(MinTest, BPicksSmaller)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(MinTest, EqualValues_ReturnsEither)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(7.0f, 7.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 7.0f);
}

TEST(MinTest, NegativeValues)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(-10.0f, -5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], -10.0f);
}

TEST(MinTest, MixedSign)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(-5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], -5.0f);
}

// =============================================================================
// Max Tests
// =============================================================================

TEST(MaxTest, APicksLarger)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(MaxTest, BPicksLarger)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

TEST(MaxTest, EqualValues_ReturnsEither)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(7.0f, 7.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 7.0f);
}

TEST(MaxTest, NegativeValues)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(-10.0f, -5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], -5.0f);
}

TEST(MaxTest, MixedSign)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(-5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

// =============================================================================
// Greater Tests
// =============================================================================

TEST(GreaterTest, AGreaterThanB_ReturnsOne)
{
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(GreaterTest, ALessThanB_ReturnsZero)
{
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(GreaterTest, Equal_ReturnsZero)
{
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

// =============================================================================
// Lesser Tests
// =============================================================================

TEST(LesserTest, ALessThanB_ReturnsOne)
{
    Lesser<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(LesserTest, AGreaterThanB_ReturnsZero)
{
    Lesser<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(LesserTest, Equal_ReturnsZero)
{
    Lesser<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

// =============================================================================
// GreaterEq Tests
// =============================================================================

TEST(GreaterEqTest, AGreaterThanB_ReturnsOne)
{
    GreaterEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(GreaterEqTest, Equal_ReturnsOne)
{
    GreaterEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(GreaterEqTest, ALessThanB_ReturnsZero)
{
    GreaterEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

// =============================================================================
// LesserEq Tests
// =============================================================================

TEST(LesserEqTest, ALessThanB_ReturnsOne)
{
    LesserEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 10.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(LesserEqTest, Equal_ReturnsOne)
{
    LesserEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(LesserEqTest, AGreaterThanB_ReturnsZero)
{
    LesserEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(10.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

// =============================================================================
// Real-World Use Cases
// =============================================================================

TEST(ComparisonComponents, PowerSupplySelection_OR)
{
    // Max acts as logical OR for voltage: choose the supply with higher voltage
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    // Bus 1: 24V, Bus 2: 26V -> should pick Bus 2
    auto st = make_state_2in(24.0f, 26.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 26.0f);
}

TEST(ComparisonComponents, SafetyMonitoring_LowLimit)
{
    // Lesser triggers alarm if value drops below threshold
    // Oil pressure < 2.0 kg/cm² -> warning
    Lesser<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    // Oil pressure 1.5 < 2.0 -> should trigger (1.0)
    auto st = make_state_2in(1.5f, 2.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(ComparisonComponents, SafetyMonitoring_HighLimit)
{
    // Greater triggers alarm if value exceeds threshold
    // Temperature > 100°C -> warning
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    // Temperature 120 > 100 -> should trigger (1.0)
    auto st = make_state_2in(120.0f, 100.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);
}

TEST(ComparisonComponents, Hysteresis_LesserEq)
{
    // LesserEq used for hysteresis: turn off when value <= lower_threshold
    // Fan turns off when temp <= 75°C
    LesserEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(75.0f, 75.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);  // Turn off condition met
}

TEST(ComparisonComponents, Hysteresis_GreaterEq)
{
    // GreaterEq used for hysteresis: turn on when value >= upper_threshold
    // Fan turns on when temp >= 80°C
    GreaterEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(80.0f, 80.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1.0f);  // Turn on condition met
}

TEST(ComparisonComponents, Min_ProtectsAgainstLowVoltage)
{
    // Min chooses the lower value - useful for limiting to safe minimum
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    // Never go below 20V (minimum safe voltage)
    auto st = make_state_2in(18.0f, 20.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 18.0f);  // Actually picks the smaller (unsafe)
}

TEST(ComparisonComponents, Min_LimitsMaximum)
{
    // Min can limit to a maximum by passing (input, max_limit)
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    // Limit voltage to maximum 30V
    auto st = make_state_2in(35.0f, 30.0f);
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 30.0f);  // Limited to max
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(ComparisonComponents, Regression_Greater_EqualValues)
{
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);  // 5 > 5 is false
}

TEST(ComparisonComponents, Regression_Lesser_EqualValues)
{
    Lesser<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);  // 5 < 5 is false
}

TEST(ComparisonComponents, Regression_GreaterEq_EqualValues)
{
    GreaterEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 1.0f);  // 5 >= 5 is true
}

TEST(ComparisonComponents, Regression_LesserEq_EqualValues)
{
    LesserEq<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(5.0f, 5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 1.0f);  // 5 <= 5 is true
}

TEST(ComparisonComponents, Regression_NegativeValues)
{
    Greater<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(-3.0f, -5.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 1.0f);  // -3 > -5 is true
}

TEST(ComparisonComponents, Regression_Min_EqualValues)
{
    Min<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(7.0f, 7.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 7.0f);
}

TEST(ComparisonComponents, Regression_Max_EqualValues)
{
    Max<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);

    auto st = make_state_2in(7.0f, 7.0f);
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 7.0f);
}
