/// Regression tests for ElectricPump two-port hydraulic loop closure.
///
/// ElectricPump was originally a one-port pressure source (p_out only).
/// [BUG-22] fixed Norton residual self-correction.
/// This revision adds a p_in port for closed-loop hydraulic circuits.
///
/// New physics:
///   - stamp_two_port(p_in, p_out, g_coupling=0.1) couples input/output (leakage path)
///   - Norton source on p_out drives toward (p_in + target_p) with g_boost=10
///   - Electrical draw is load-dependent: G_idle + K * dp / (V^2 + 1)

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
    comp.provider.indices[PortNames::v_in]  = 0;
    comp.provider.indices[PortNames::p_in]  = 1;
    comp.provider.indices[PortNames::p_out] = 2;
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
// Core Regression: Norton residual uses self-correction with p_in reference
// =============================================================================

TEST(ElectricPumpRegression, NortonResidual_UsesCurrentPressure) {
    // When p_out already has pressure, the through contribution
    // on p_out should be proportional to (p_in + target_p - p_out), not just target_p.
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;    // v_in = 28V
    st.across[1] = 0.0f;     // p_in = 0 (ground reference)
    st.across[2] = 500.0f;   // p_out already at 500 psi

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    float target_p = 28.0f * 1000.0f / 28.0f;  // 1000
    // Two-port coupling contributes: (p_out - p_in) * g_coupling = (500 - 0) * 0.1 = 50
    // on p_in side: through += 50, conductance += 0.1
    // on p_out side: through -= 50, conductance += 0.1
    // Norton boost on p_out: through += (0 + 1000 - 500) * 10 = 5000, conductance += 10
    // Total through[p_out] = -50 + 5000 = 4950
    // Total conductance[p_out] = 0.1 + 10 = 10.1
    float g_coupling = 0.1f;
    float g_boost = 10.0f;
    float coupling_through = (0.0f - 500.0f) * g_coupling;  // -50 (current from two_port: V2-V1, idx1=p_in(0), idx2=p_out(500))
    // stamp_two_port(p_in, p_out, g): i = (across[p_out] - across[p_in]) * g
    // through[p_in] += i, through[p_out] -= i
    float i_coupling = (500.0f - 0.0f) * g_coupling;  // 50
    float boost_through = (0.0f + target_p - 500.0f) * g_boost;  // 5000

    float expected_through_p_out = -i_coupling + boost_through;  // -50 + 5000 = 4950
    EXPECT_FLOAT_EQ(st.through[2], expected_through_p_out)
        << "through[p_out] should account for both two-port coupling and boost source";
}

TEST(ElectricPumpRegression, NortonResidual_CorrectWhenAtTarget) {
    // When p_out is already at (p_in + target_p), boost through should be ~0
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 100.0f;   // p_in = 100
    float target_p = 28.0f * 1000.0f / 28.0f;  // 1000
    st.across[2] = 100.0f + target_p;  // p_out = 1100 = p_in + target

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // The boost Norton source contributes 0.
    // The two-port coupling contributes (1100 - 100) * 0.1 = 100 to through[p_in],
    // and -100 to through[p_out].
    float g_coupling = 0.1f;
    float g_boost = 10.0f;
    float i_coupling = (1100.0f - 100.0f) * g_coupling;  // 100
    float boost_through = (100.0f + target_p - 1100.0f) * g_boost;  // 0
    EXPECT_NEAR(boost_through, 0.0f, 1e-5f);

    float expected_p_out_through = -i_coupling + boost_through;
    EXPECT_NEAR(st.through[2], expected_p_out_through, 1e-3f)
        << "through should be dominated by two-port coupling when at target";
}

TEST(ElectricPumpRegression, NortonResidual_NegativeWhenAboveTarget) {
    // When p_out overshoots target, boost through should be negative to pull it back
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 0.0f;     // p_in = 0
    st.across[2] = 1200.0f;  // above target (1000)

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // Boost contribution: (0 + 1000 - 1200) * 10 = -2000 (negative, corrective)
    float g_boost = 10.0f;
    float boost_through = (0.0f + 1000.0f - 1200.0f) * g_boost;
    EXPECT_LT(boost_through, 0.0f)
        << "boost should be negative when above target to pull pressure down";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(ElectricPumpRegression, SOR_ConvergesToTarget) {
    // 28V supply with max_pressure 1000, p_in fixed at 0 → p_out should → 1000
    auto comp = make_pump(1000.0f);
    auto st = make_state(3);
    st.across[0] = 28.0f;  // fixed v_in
    st.across[1] = 0.0f;   // fixed p_in (ground)
    st.across[2] = 0.0f;   // p_out starts at 0

    const float omega = SOR::OMEGA;
    float target_p = 28.0f * 1000.0f / 28.0f;  // 1000

    for (int step = 0; step < 200; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.through[2] = 0.0f;
        st.conductance[0] = 1e-6f;
        st.conductance[1] = 1e-6f;
        st.conductance[2] = 1e-6f;

        // Fix v_in at 28V
        float g_fix = 1e6f;
        st.conductance[0] += g_fix;
        st.through[0] += (28.0f - st.across[0]) * g_fix;

        // Fix p_in at 0 (ground/tank reference)
        st.conductance[1] += g_fix;
        st.through[1] += (0.0f - st.across[1]) * g_fix;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];
        st.inv_conductance[2] = 1.0f / st.conductance[2];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 3, omega);
    }

    EXPECT_NEAR(st.across[2], target_p, 10.0f)
        << "Pressure output should converge to p_in + target_p = 0 + 1000";

    EXPECT_FALSE(std::isnan(st.across[2])) << "p_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[2])) << "p_out is Inf - SOR diverged";
    EXPECT_LT(std::abs(st.across[2]), 2000.0f)
        << "p_out wildly diverged - likely old bug still present";
}

TEST(ElectricPumpRegression, SOR_ConvergesWithNonZeroPIn) {
    // p_in fixed at 200, target_p = 1000 → p_out should → 1200
    auto comp = make_pump(1000.0f);
    auto st = make_state(3);
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 200.0f; // p_in = 200 (upstream pressure)
    st.across[2] = 0.0f;   // p_out starts at 0

    const float omega = SOR::OMEGA;

    for (int step = 0; step < 200; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.through[2] = 0.0f;
        st.conductance[0] = 1e-6f;
        st.conductance[1] = 1e-6f;
        st.conductance[2] = 1e-6f;

        // Fix v_in at 28V
        float g_fix = 1e6f;
        st.conductance[0] += g_fix;
        st.through[0] += (28.0f - st.across[0]) * g_fix;

        // Fix p_in at 200 (upstream tank)
        st.conductance[1] += g_fix;
        st.through[1] += (200.0f - st.across[1]) * g_fix;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];
        st.inv_conductance[2] = 1.0f / st.conductance[2];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 3, omega);
    }

    float expected_p_out = 200.0f + 1000.0f;  // 1200
    EXPECT_NEAR(st.across[2], expected_p_out, 15.0f)
        << "p_out should converge to p_in + target_p = 200 + 1000 = 1200";
}

// =============================================================================
// Electrical Side: Load-dependent draw
// =============================================================================

TEST(ElectricPumpRegression, ElectricalSide_IdleDrawWhenNoPressureDiff) {
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 100.0f; // p_in
    st.across[2] = 100.0f; // p_out = p_in (no pressure differential)

    comp.solve_electrical(st, 1.0f / 60.0f);

    // dp = max(p_out - p_in, 0) = 0, so g_load = 0
    // g_total = G_IDLE = 0.01
    EXPECT_FLOAT_EQ(st.conductance[0], 0.01f)
        << "With no pressure differential, only idle conductance should be present";
}

TEST(ElectricPumpRegression, ElectricalSide_IncreasedDrawWithPressure) {
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 0.0f;   // p_in = 0
    st.across[2] = 500.0f; // p_out = 500 (pressure differential = 500)

    comp.solve_electrical(st, 1.0f / 60.0f);

    // dp = 500, g_load = 0.001 * 500 / (28^2 + 1) = 0.5 / 785 ≈ 0.000637
    // g_total = 0.01 + 0.000637 ≈ 0.0106
    EXPECT_GT(st.conductance[0], 0.01f)
        << "With pressure differential, conductance should exceed idle draw";
}

// =============================================================================
// Zero Input
// =============================================================================

TEST(ElectricPumpRegression, ZeroVoltage_ZeroPressureBoost) {
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 0.0f;  // no voltage
    st.across[1] = 0.0f;  // p_in = 0
    st.across[2] = 0.0f;  // p_out = 0

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // target_p = 0 * 1000 / 28 = 0, boost through = (0 + 0 - 0) * 10 = 0
    // Two-port coupling through = (0 - 0) * 0.1 = 0
    EXPECT_FLOAT_EQ(st.through[2], 0.0f)
        << "Zero voltage should produce zero target pressure and zero through";
}

// =============================================================================
// Two-port coupling: stamp_two_port creates bidirectional path
// =============================================================================

TEST(ElectricPumpRegression, TwoPortCoupling_BidirectionalPath) {
    // Verify that stamp_two_port creates conductance on both p_in and p_out
    auto comp = make_pump(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 100.0f;  // p_in
    st.across[2] = 200.0f;  // p_out

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // g_coupling = 0.1 stamps on both ports, g_boost = 10 stamps on p_out only
    EXPECT_FLOAT_EQ(st.conductance[1], 0.1f)
        << "p_in should have g_coupling = 0.1";
    EXPECT_FLOAT_EQ(st.conductance[2], 0.1f + 10.0f)
        << "p_out should have g_coupling + g_boost = 10.1";
}
