/// Regression tests for [BUG-22]: ElectricPump::solve_hydraulic() Norton residual
/// missing self-correction term.
///
/// The old code computed:
///   through[p_out] += target_p * g
/// instead of:
///   through[p_out] += (target_p - p_out) * g
///
/// Without the self-correction term, the SOR accumulates target_p each
/// iteration instead of converging to it, causing pressure divergence.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static ElectricPump<JitProvider> make_pump(float max_p = 1000.0f) {
    ElectricPump<JitProvider> comp;
    comp.max_pressure = max_p;
    comp.provider.indices[PortNames::v_in] = 0;
    comp.provider.indices[PortNames::p_out] = 1;
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

TEST(ElectricPumpRegression, NortonResidual_UsesCurrentPressure) {
    // When p_out already has pressure, the through contribution
    // should be proportional to (target_p - p_current), not just target_p.
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;    // v_in = 28V
    st.across[1] = 500.0f;   // p_out already at 500 psi

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    float target_p = 28.0f * 1000.0f / 28.0f;  // 1000
    float expected_through = (target_p - 500.0f) * 1.0f;  // 500

    EXPECT_FLOAT_EQ(st.through[1], expected_through)
        << "through[p_out] should be (target_p - p_current) * g, not target_p * g";
}

TEST(ElectricPumpRegression, NortonResidual_CorrectWhenAtTarget) {
    // When p_out is already at the target pressure, through should be ~0
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    float target_p = 28.0f * 1000.0f / 28.0f;
    st.across[1] = target_p;  // already at target

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[1], 0.0f, 1e-5f)
        << "through should be ~0 when already at target pressure";
}

TEST(ElectricPumpRegression, NortonResidual_NegativeWhenAboveTarget) {
    // When p_out overshoots, through should be negative to pull it back
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 1200.0f;  // above target (1000)

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_LT(st.through[1], 0.0f)
        << "through should be negative when above target to pull pressure down";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(ElectricPumpRegression, SOR_ConvergesToTarget) {
    // 28V supply with max_pressure 1000 → target pressure = 1000
    // SOR should converge p_out to the target.
    auto comp = make_pump(1000.0f);
    auto st = make_state(2);
    st.across[0] = 28.0f;  // fixed v_in
    st.across[1] = 0.0f;   // p_out starts at 0

    const float omega = SOR::OMEGA;
    float target_p = 28.0f * 1000.0f / 28.0f;  // 1000

    for (int step = 0; step < 100; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.conductance[0] = 1e-6f;  // parasitic
        st.conductance[1] = 1e-6f;

        // Fix v_in at 28V: add huge conductance + through to keep it there
        float g_fix = 1e6f;
        st.conductance[0] += g_fix;
        st.through[0] += (28.0f - st.across[0]) * g_fix;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 2, omega);
    }

    EXPECT_NEAR(st.across[1], target_p, 5.0f)
        << "Pressure output should converge to target_p";

    // Old bug: without self-correction, p_out would diverge
    EXPECT_FALSE(std::isnan(st.across[1])) << "p_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[1])) << "p_out is Inf - SOR diverged";
    EXPECT_LT(std::abs(st.across[1]), 2000.0f)
        << "p_out wildly diverged - likely old bug still present";
}

// =============================================================================
// Electrical Side Unaffected
// =============================================================================

TEST(ElectricPumpRegression, ElectricalSide_DrawsConstantCurrent) {
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    // G_MOTOR = 0.01
    EXPECT_FLOAT_EQ(st.conductance[0], 0.01f);
}

// =============================================================================
// Zero Input
// =============================================================================

TEST(ElectricPumpRegression, ZeroVoltage_ZeroPressure) {
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 0.0f;  // no voltage
    st.across[1] = 0.0f;  // no pressure

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_FLOAT_EQ(st.through[1], 0.0f)
        << "Zero voltage should produce zero target pressure and zero through";
}
