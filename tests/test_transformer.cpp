#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static Transformer<JitProvider> make_transformer(float ratio = 1.0f) {
    Transformer<JitProvider> comp;
    comp.ratio = ratio;
    comp.provider.indices[PortNames::primary] = 0;
    comp.provider.indices[PortNames::secondary] = 1;
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Basic Voltage Ratio Tests
// =============================================================================

TEST(TransformerTest, Unity_Ratio_PassesVoltageThrough) {
    auto comp = make_transformer(1.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_primary = 28V

    comp.solve_electrical(st, 1.0f / 60.0f);

    // Secondary through should drive toward v_primary * 1.0 = 28V
    // through[secondary] = 28.0 * 1.0 = 28.0
    EXPECT_GT(st.through[1], 0.0f);  // Positive current drives secondary up
}

TEST(TransformerTest, StepDown_Ratio_HalvesVoltage) {
    auto comp = make_transformer(0.5f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_primary = 28V

    comp.solve_electrical(st, 1.0f / 60.0f);

    // Secondary through should drive toward 28 * 0.5 = 14V
    float expected_v_secondary_target = 28.0f * 0.5f;
    // through[secondary] = expected_v_secondary_target * g_secondary
    EXPECT_FLOAT_EQ(st.through[1], expected_v_secondary_target * 1.0f);
}

TEST(TransformerTest, StepUp_Ratio_DoublesVoltage) {
    auto comp = make_transformer(2.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_primary = 28V

    comp.solve_electrical(st, 1.0f / 60.0f);

    // Secondary through should drive toward 28 * 2.0 = 56V
    float expected_v_secondary_target = 28.0f * 2.0f;
    EXPECT_FLOAT_EQ(st.through[1], expected_v_secondary_target * 1.0f);
}

// =============================================================================
// Energy Conservation Tests (the critical regression tests)
// =============================================================================

TEST(TransformerTest, EnergyConservation_ReflectedConductance) {
    // For an ideal transformer: g_primary = g_secondary * ratio²
    // This is the impedance transformation law.
    auto comp = make_transformer(2.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_primary

    comp.solve_electrical(st, 1.0f / 60.0f);

    // g_secondary = 1.0
    float g_secondary = 1.0f;
    float expected_g_primary = g_secondary * 2.0f * 2.0f;  // 4.0

    EXPECT_FLOAT_EQ(st.conductance[0], expected_g_primary);
    EXPECT_FLOAT_EQ(st.conductance[1], g_secondary);
}

TEST(TransformerTest, EnergyConservation_PrimaryCurrentReflectsLoad) {
    // Key regression test: the old bug was that i_primary = i_secondary
    // instead of i_primary = i_secondary * ratio (from P_in = P_out).
    //
    // With a 2:1 step-up transformer:
    //   V_secondary = 2 * V_primary
    //   I_primary should be ratio² * V_primary * g = 4 * 28 * 1 = 112
    //   I_secondary should be ratio * V_primary * g = 2 * 28 * 1 = 56
    //
    // Power check: P_primary = I_primary * V_primary = 112 * 28 = 3136
    //              P_secondary = I_secondary * V_secondary = 56 * 56 = 3136 ✓
    auto comp = make_transformer(2.0f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // Primary draws current proportional to ratio²
    float g_primary_expected = 1.0f * 2.0f * 2.0f;
    float i_primary_expected = 28.0f * g_primary_expected;
    EXPECT_FLOAT_EQ(st.through[0], -i_primary_expected);  // Negative = drains from primary

    // Secondary receives voltage-driving current
    float i_secondary_expected = 28.0f * 2.0f * 1.0f;
    EXPECT_FLOAT_EQ(st.through[1], i_secondary_expected);
}

TEST(TransformerTest, EnergyConservation_StepDown) {
    // Step-down 0.5:1 transformer
    // g_primary = g_secondary * 0.5² = 0.25
    // i_primary = v_primary * g_primary = 28 * 0.25 = 7
    // i_secondary = v_secondary_target * g_secondary = 14 * 1 = 14
    // P_primary = 7 * 28 = 196
    // P_secondary = 14 * 14 = 196 ✓
    auto comp = make_transformer(0.5f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.conductance[0], 0.25f);  // g * ratio²
    EXPECT_FLOAT_EQ(st.conductance[1], 1.0f);   // g_secondary
    EXPECT_FLOAT_EQ(st.through[0], -7.0f);      // drains from primary
    EXPECT_FLOAT_EQ(st.through[1], 14.0f);      // drives secondary
}

TEST(TransformerTest, ZeroPrimaryVoltage_ZeroEverywhere) {
    auto comp = make_transformer(2.0f);
    auto st = make_state();
    st.across[0] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // No voltage → no current transfer, but conductance is still stamped
    EXPECT_FLOAT_EQ(st.through[0], 0.0f);
    EXPECT_FLOAT_EQ(st.through[1], 0.0f);
    EXPECT_GT(st.conductance[0], 0.0f);  // Conductance always stamped
    EXPECT_GT(st.conductance[1], 0.0f);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(TransformerTest, UnityRatio_SymmetricConductance) {
    // ratio=1 → g_primary = g_secondary * 1² = g_secondary
    auto comp = make_transformer(1.0f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.conductance[0], st.conductance[1]);
}

TEST(TransformerTest, VeryHighRatio_LargeReflectedConductance) {
    // High turns ratio → much larger conductance on primary
    auto comp = make_transformer(10.0f);
    auto st = make_state();
    st.across[0] = 10.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // g_primary = 1.0 * 100 = 100
    EXPECT_FLOAT_EQ(st.conductance[0], 100.0f);
    EXPECT_FLOAT_EQ(st.conductance[1], 1.0f);
}

TEST(TransformerTest, VeryLowRatio_SmallReflectedConductance) {
    // Low turns ratio → smaller conductance on primary
    auto comp = make_transformer(0.1f);
    auto st = make_state();
    st.across[0] = 100.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // g_primary = 1.0 * 0.01 = 0.01
    EXPECT_FLOAT_EQ(st.conductance[0], 0.01f);
    EXPECT_FLOAT_EQ(st.conductance[1], 1.0f);
}
