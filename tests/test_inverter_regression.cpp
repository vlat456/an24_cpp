/// Regression tests for [BUG-Inverter]: Norton residual missing self-correction.
///
/// The old Inverter::solve_electrical() computed:
///   through[ac_out] += v_ac * g
/// instead of:
///   through[ac_out] += (v_ac_target - v_ac_current) * g
///
/// Without the self-correction term, the SOR accumulated v_ac_target each
/// iteration instead of converging to it, causing voltage divergence.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static Inverter<JitProvider> make_inverter(float eff = 0.95f) {
    Inverter<JitProvider> comp;
    comp.efficiency = eff;
    comp.provider.indices[PortNames::dc_in] = 0;
    comp.provider.indices[PortNames::ac_out] = 1;
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    st.inv_conductance.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Core Regression: Norton residual uses self-correction
// =============================================================================

TEST(InverterRegression, NortonResidual_UsesCurrentVoltage) {
    // When ac_out already has some voltage, the through contribution
    // should be proportional to (v_target - v_current), not just v_target.
    auto comp = make_inverter(0.95f);
    auto st = make_state();
    st.across[0] = 28.0f;   // dc_in = 28V
    st.across[1] = 25.0f;   // ac_out already at 25V (from previous SOR iteration)

    comp.solve_electrical(st, 1.0f / 60.0f);

    float v_target = 28.0f * 0.95f;  // 26.6V
    float expected_through = (v_target - 25.0f) * 1.0f;  // 1.6

    EXPECT_FLOAT_EQ(st.through[1], expected_through)
        << "through[ac_out] should be (v_target - v_current) * g, not v_target * g";
}

TEST(InverterRegression, NortonResidual_CorrectWhenAtTarget) {
    // When ac_out is already at the target voltage, through should be ~0
    auto comp = make_inverter(0.95f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 28.0f * 0.95f;  // already at target

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.through[1], 0.0f, 1e-5f)
        << "through should be ~0 when already at target voltage";
}

TEST(InverterRegression, NortonResidual_NegativeWhenAboveTarget) {
    // When ac_out overshoots, through should be negative to pull it back
    auto comp = make_inverter(0.95f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 30.0f;  // above target (26.6V)

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_LT(st.through[1], 0.0f)
        << "through should be negative when above target to pull voltage down";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(InverterRegression, SOR_ConvergesToTarget) {
    // DC supply at 28V with efficiency 0.95 → target AC = 26.6V
    // SOR should converge ac_out to the target.
    auto comp = make_inverter(0.95f);
    auto st = make_state(2);
    st.across[0] = 28.0f;  // fixed DC input
    st.across[1] = 0.0f;   // AC starts at 0

    const float omega = SOR::OMEGA;
    float v_target = 28.0f * 0.95f;

    for (int step = 0; step < 100; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.conductance[0] = 1e-6f;  // parasitic
        st.conductance[1] = 1e-6f;

        // Fix dc_in at 28V: add huge conductance + through to keep it there
        float g_fix = 1e6f;
        st.conductance[0] += g_fix;
        st.through[0] += (28.0f - st.across[0]) * g_fix;

        comp.solve_electrical(st, 1.0f / 60.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 2, omega);
    }

    EXPECT_NEAR(st.across[1], v_target, 0.5f)
        << "AC output should converge to dc_in * efficiency";

    // Old bug: without self-correction, ac_out would diverge
    EXPECT_FALSE(std::isnan(st.across[1])) << "AC output is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[1])) << "AC output is Inf - SOR diverged";
    EXPECT_LT(std::abs(st.across[1]), 100.0f)
        << "AC output wildly diverged — likely old bug still present";
}

// =============================================================================
// DC Input Load Drawing
// =============================================================================

TEST(InverterRegression, DCInput_DrawsProportionalLoad) {
    auto comp = make_inverter(0.95f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // g_dc = g * efficiency = 1.0 * 0.95 = 0.95
    EXPECT_FLOAT_EQ(st.conductance[0], 0.95f);

    // through[dc_in] = -v_dc * g_dc = -28 * 0.95 = -26.6
    EXPECT_FLOAT_EQ(st.through[0], -28.0f * 0.95f);
}

TEST(InverterRegression, ZeroInput_ZeroOutput) {
    auto comp = make_inverter(0.95f);
    auto st = make_state();
    st.across[0] = 0.0f;
    st.across[1] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.through[0], 0.0f);
    EXPECT_FLOAT_EQ(st.through[1], 0.0f);
}
