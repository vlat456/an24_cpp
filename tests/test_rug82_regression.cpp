/// Regression tests for [BUG-RUG82]: Integration per SOR iteration.
///
/// The old RUG82::solve_electrical() contained:
///   k_mod += kp * error * dt
/// Since solve_electrical runs on every SOR iteration, the effective
/// integral gain was N * kp (where N = iteration count), making the
/// regulator non-deterministic.
///
/// The fix moves integration to finalize_step() (runs once per frame),
/// and solve_electrical() only writes the current k_mod output.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static RUG82<JitProvider> make_rug82(float v_target = 28.5f, float kp = 2.0f) {
    RUG82<JitProvider> comp;
    comp.v_target = v_target;
    comp.kp = kp;
    comp.k_mod = 0.5f;
    comp.provider.indices[PortNames::v_gen] = 0;
    comp.provider.indices[PortNames::k_mod] = 1;
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
// Core Regression: Integration in finalize_step, not solve_electrical
// =============================================================================

TEST(RUG82Regression, SolveElectrical_DoesNotIntegrate) {
    // Calling solve_electrical multiple times should NOT change k_mod
    auto comp = make_rug82();
    auto st = make_state();
    st.across[0] = 25.0f;  // v_gen = 25V, error = 3.5V

    float initial_k_mod = comp.k_mod;

    // Simulate 10 SOR iterations calling solve_electrical
    for (int i = 0; i < 10; ++i) {
        comp.solve_electrical(st, 1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(comp.k_mod, initial_k_mod)
        << "k_mod should NOT change during solve_electrical (was the old bug)";
}

TEST(RUG82Regression, FinalizePhase_IntegratesOnce) {
    auto comp = make_rug82(28.5f, 2.0f);
    auto st = make_state();
    st.across[0] = 25.0f;  // v_gen = 25V, error = 3.5V
    float dt = 1.0f / 60.0f;

    float initial_k_mod = comp.k_mod;

    comp.finalize_step(st, dt);

    float expected = initial_k_mod + 2.0f * 3.5f * dt;
    expected = std::clamp(expected, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(comp.k_mod, expected)
        << "k_mod should integrate error once in finalize_step";
}

TEST(RUG82Regression, FinalizePhase_MultipleCallsAccumulate) {
    auto comp = make_rug82(28.5f, 2.0f);
    auto st = make_state();
    st.across[0] = 27.0f;  // error = 1.5
    float dt = 1.0f / 60.0f;

    float k0 = comp.k_mod;

    comp.finalize_step(st, dt);
    float k1 = comp.k_mod;

    comp.finalize_step(st, dt);
    float k2 = comp.k_mod;

    float delta1 = k1 - k0;
    float delta2 = k2 - k1;

    // Each call should integrate the same amount (same error, same dt)
    EXPECT_NEAR(delta1, delta2, 1e-6f)
        << "Each finalize_step call should integrate the same delta";
}

TEST(RUG82Regression, SolveElectrical_WritesKmod) {
    // solve_electrical should still write k_mod to the output signal
    auto comp = make_rug82();
    comp.k_mod = 0.75f;
    auto st = make_state();

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.75f)
        << "solve_electrical should write current k_mod to output";
}

TEST(RUG82Regression, FinalizePhase_ClampsKmod) {
    // k_mod should be clamped to [0, 1]
    auto comp = make_rug82(28.5f, 100.0f);  // huge gain
    auto st = make_state();
    st.across[0] = 0.0f;  // huge error
    float dt = 1.0f;

    comp.finalize_step(st, dt);

    EXPECT_LE(comp.k_mod, 1.0f) << "k_mod should be clamped to 1.0 max";
    EXPECT_GE(comp.k_mod, 0.0f) << "k_mod should be clamped to 0.0 min";
}

// =============================================================================
// Determinism: same result regardless of SOR iteration count
// =============================================================================

TEST(RUG82Regression, Deterministic_IndependentOfIterationCount) {
    // The key regression: with the old bug, more SOR iterations meant
    // more integration steps, making k_mod non-deterministic.
    float dt = 1.0f / 60.0f;

    // Run with 5 "SOR iterations" + 1 finalize_step
    auto comp5 = make_rug82();
    auto st5 = make_state();
    st5.across[0] = 25.0f;
    for (int i = 0; i < 5; ++i) comp5.solve_electrical(st5, dt);
    comp5.finalize_step(st5, dt);

    // Run with 50 "SOR iterations" + 1 finalize_step
    auto comp50 = make_rug82();
    auto st50 = make_state();
    st50.across[0] = 25.0f;
    for (int i = 0; i < 50; ++i) comp50.solve_electrical(st50, dt);
    comp50.finalize_step(st50, dt);

    EXPECT_FLOAT_EQ(comp5.k_mod, comp50.k_mod)
        << "k_mod must be identical regardless of SOR iteration count";
}
