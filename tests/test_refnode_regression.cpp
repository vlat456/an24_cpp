/// Regression tests for [BUG-RefNode]: Missing Norton residual subtraction.
///
/// The old RefNode::solve_electrical() computed:
///   through[v] += value * g
///   conductance[v] += g
/// Without the -across[v]*g self-correction term, the SOR iteration added
/// value*omega to the voltage every iteration, causing unbounded divergence
/// for any RefNode with value != 0.
///
/// The fix replaces the manual stamp with stamp_voltage_source(), which
/// correctly computes through = (v_source - v_current) * g.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static RefNode<JitProvider> make_refnode(float value) {
    RefNode<JitProvider> comp;
    comp.value = value;
    comp.provider.indices[PortNames::v] = 0;
    return comp;
}

static SimulationState make_state(size_t n = 2) {
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

TEST(RefNodeRegression, NortonResidual_UsesCurrentVoltage) {
    // When the node already has voltage, through should be (value - v_current) * g
    auto comp = make_refnode(28.5f);
    auto st = make_state();
    st.across[0] = 25.0f;  // node already at 25V

    comp.solve_electrical(st, 1.0f / 60.0f);

    // stamp_voltage_source with r=1e-6 gives g=1e6
    float g = 1.0f / 1e-6f;
    float expected_through = (28.5f - 25.0f) * g;

    EXPECT_NEAR(st.through[0], expected_through, 1.0f)
        << "through[v] should be (value - v_current) * g, not value * g";
}

TEST(RefNodeRegression, NortonResidual_ZeroWhenAtTarget) {
    auto comp = make_refnode(28.5f);
    auto st = make_state();
    st.across[0] = 28.5f;  // already at target

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.through[0], 0.0f, 1.0f)
        << "through should be ~0 when node is already at target value";
}

TEST(RefNodeRegression, NortonResidual_NegativeWhenAboveTarget) {
    auto comp = make_refnode(28.5f);
    auto st = make_state();
    st.across[0] = 35.0f;  // above target

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_LT(st.through[0], 0.0f)
        << "through should be negative when node is above target";
}

TEST(RefNodeRegression, GroundRef_ZeroValue_AlwaysZeroThrough) {
    // Ground reference (value=0) should always have zero through at V=0
    auto comp = make_refnode(0.0f);
    auto st = make_state();
    st.across[0] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.through[0], 0.0f, 1e-6f)
        << "Ground reference at 0V should have zero through";
}

TEST(RefNodeRegression, GroundRef_PullsToZero) {
    // Ground reference (value=0) should pull node toward 0V
    auto comp = make_refnode(0.0f);
    auto st = make_state();
    st.across[0] = 10.0f;  // node drifted to 10V

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_LT(st.through[0], 0.0f)
        << "Ground reference should pull positive voltage back toward 0";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(RefNodeRegression, SOR_ConvergesToValue) {
    // RefNode with value=28.5 should drive node to 28.5V via SOR
    auto comp = make_refnode(28.5f);
    auto st = make_state();
    st.across[0] = 0.0f;  // starts at 0V

    const float omega = SOR::OMEGA;

    for (int step = 0; step < 50; ++step) {
        st.through[0] = 0.0f;
        st.conductance[0] = 1e-6f;  // parasitic

        comp.solve_electrical(st, 1.0f / 60.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 1, omega);
    }

    EXPECT_NEAR(st.across[0], 28.5f, 0.1f)
        << "RefNode should converge to its value";
    EXPECT_FALSE(std::isnan(st.across[0])) << "SOR diverged to NaN";
    EXPECT_FALSE(std::isinf(st.across[0])) << "SOR diverged to Inf";
}

TEST(RefNodeRegression, SOR_NonZeroValue_DoesNotDiverge) {
    // This is the key regression: the old bug caused divergence for value != 0
    auto comp = make_refnode(115.0f);
    auto st = make_state();
    st.across[0] = 0.0f;

    const float omega = SOR::OMEGA;

    for (int step = 0; step < 200; ++step) {
        st.through[0] = 0.0f;
        st.conductance[0] = 1e-6f;

        comp.solve_electrical(st, 1.0f / 60.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 1, omega);
    }

    EXPECT_NEAR(st.across[0], 115.0f, 0.5f)
        << "RefNode at 115V should converge, not diverge";
    EXPECT_LT(std::abs(st.across[0]), 200.0f)
        << "Voltage diverged — old bug may still be present";
}
