/// Regression tests for FuelTank component.
///
/// Tests verify:
/// - Gravity head pressure computation (P = rho * g * level_frac)
/// - Norton self-correction on flow_out (prevents SOR divergence)
/// - level_out signal outputs fuel level fraction [0..1]
/// - Fuel consumption in finalize_step (level decreases with flow)
/// - pre_load() clamping and inv_capacity computation
/// - SOR convergence

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static FuelTank<JitProvider> make_tank(
    float capacity = 1000.0f, float level = 1000.0f, float density = 0.78f
) {
    FuelTank<JitProvider> comp;
    comp.capacity = capacity;
    comp.level = level;
    comp.density = density;
    comp.provider.indices[PortNames::flow_out]  = 0;
    comp.provider.indices[PortNames::level_out] = 1;
    comp.pre_load();
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
// Gravity Head Pressure
// =============================================================================

TEST(FuelTankRegression, GravityPressure_FullTank) {
    // Full tank: level_frac = 1.0
    // P = density * 9.81 * 1.0 = 0.78 * 9.81 = 7.6518
    auto comp = make_tank(1000.0f, 1000.0f, 0.78f);
    auto st = make_state();
    st.across[0] = 0.0f;  // flow_out initially 0

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    float expected_p = 0.78f * 9.81f * 1.0f;
    // Norton source: (expected_p - 0) * 1 = expected_p
    EXPECT_NEAR(st.through[0], expected_p, 0.01f)
        << "Full tank should produce gravity head pressure of ~7.65";
    EXPECT_GT(st.conductance[0], 0.0f)
        << "Conductance should be stamped on flow_out";
}

TEST(FuelTankRegression, GravityPressure_HalfTank) {
    auto comp = make_tank(1000.0f, 500.0f, 0.78f);
    auto st = make_state();
    st.across[0] = 0.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    float expected_p = 0.78f * 9.81f * 0.5f;
    EXPECT_NEAR(st.through[0], expected_p, 0.01f)
        << "Half tank should produce half the gravity head pressure";
}

TEST(FuelTankRegression, GravityPressure_EmptyTank) {
    auto comp = make_tank(1000.0f, 0.0f, 0.78f);
    auto st = make_state();
    st.across[0] = 0.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[0], 0.0f, 1e-6f)
        << "Empty tank should produce zero pressure";
}

// =============================================================================
// Norton Self-Correction
// =============================================================================

TEST(FuelTankRegression, NortonResidual_SelfCorrection) {
    // When flow_out voltage equals gravity pressure, through should be ~0
    auto comp = make_tank(1000.0f, 1000.0f, 0.78f);
    auto st = make_state();
    float gravity_p = 0.78f * 9.81f * 1.0f;
    st.across[0] = gravity_p;  // at equilibrium

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[0], 0.0f, 1e-4f)
        << "At equilibrium, through should be ~0 (self-correction)";
}

TEST(FuelTankRegression, NortonResidual_NegativeWhenOverpressure) {
    auto comp = make_tank(1000.0f, 1000.0f, 0.78f);
    auto st = make_state();
    st.across[0] = 100.0f;  // much higher than gravity head (~7.65)

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_LT(st.through[0], 0.0f)
        << "When flow_out > gravity pressure, through should pull it down";
}

// =============================================================================
// Level Output
// =============================================================================

TEST(FuelTankRegression, LevelOut_FullTank) {
    auto comp = make_tank(1000.0f, 1000.0f);
    auto st = make_state();

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_FLOAT_EQ(st.across[1], 1.0f)
        << "Full tank level_out should be 1.0";
}

TEST(FuelTankRegression, LevelOut_HalfTank) {
    auto comp = make_tank(1000.0f, 500.0f);
    auto st = make_state();

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.5f)
        << "Half tank level_out should be 0.5";
}

TEST(FuelTankRegression, LevelOut_EmptyTank) {
    auto comp = make_tank(1000.0f, 0.0f);
    auto st = make_state();

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_FLOAT_EQ(st.across[1], 0.0f)
        << "Empty tank level_out should be 0.0";
}

// =============================================================================
// Fuel Consumption (finalize_step)
// =============================================================================

TEST(FuelTankRegression, FinalizePhase_ConsumesFuel) {
    auto comp = make_tank(1000.0f, 1000.0f);
    auto st = make_state();
    st.through[0] = 10.0f;  // positive flow out of tank
    float dt = 1.0f;

    float initial_level = comp.level;
    comp.finalize_step(st, dt);

    EXPECT_LT(comp.level, initial_level)
        << "Fuel level should decrease when flow is positive";
    EXPECT_FLOAT_EQ(comp.level, 1000.0f - 10.0f * 1.0f)
        << "Level should decrease by flow * dt";
}

TEST(FuelTankRegression, FinalizePhase_NegativeFlow_NoConsumption) {
    auto comp = make_tank(1000.0f, 1000.0f);
    auto st = make_state();
    st.through[0] = -5.0f;  // negative flow (shouldn't consume fuel)
    float dt = 1.0f;

    float initial_level = comp.level;
    comp.finalize_step(st, dt);

    EXPECT_FLOAT_EQ(comp.level, initial_level)
        << "Negative flow should not consume fuel";
}

TEST(FuelTankRegression, FinalizePhase_NeverGoesBelowZero) {
    auto comp = make_tank(1000.0f, 1.0f);  // nearly empty
    auto st = make_state();
    st.through[0] = 100.0f;  // huge consumption
    float dt = 1.0f;

    comp.finalize_step(st, dt);

    EXPECT_GE(comp.level, 0.0f)
        << "Fuel level should never go below zero";
}

// =============================================================================
// pre_load()
// =============================================================================

TEST(FuelTankRegression, PreLoad_ComputesInvCapacity) {
    auto comp = make_tank(500.0f, 500.0f);
    EXPECT_NEAR(comp.inv_capacity, 1.0f / 500.0f, 1e-8f)
        << "pre_load should set inv_capacity = 1/capacity";
}

TEST(FuelTankRegression, PreLoad_ClampsLevel) {
    FuelTank<JitProvider> comp;
    comp.capacity = 100.0f;
    comp.level = 200.0f;  // above capacity
    comp.pre_load();
    EXPECT_LE(comp.level, comp.capacity)
        << "pre_load should clamp level to capacity";

    comp.level = -10.0f;
    comp.pre_load();
    EXPECT_GE(comp.level, 0.0f)
        << "pre_load should clamp level >= 0";
}

TEST(FuelTankRegression, PreLoad_SafeCapacity) {
    FuelTank<JitProvider> comp;
    comp.capacity = 0.0f;
    comp.pre_load();
    EXPECT_GT(comp.inv_capacity, 0.0f)
        << "pre_load should handle zero capacity safely";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(FuelTankRegression, SOR_ConvergesToGravityPressure) {
    auto comp = make_tank(1000.0f, 1000.0f, 0.78f);
    auto st = make_state(2);
    st.across[0] = 0.0f;  // flow_out starts at 0
    st.across[1] = 0.0f;  // level_out

    const float omega = SOR::OMEGA;
    float expected_p = 0.78f * 9.81f * 1.0f;  // ~7.65

    for (int step = 0; step < 100; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.conductance[0] = 1e-6f;
        st.conductance[1] = 1e-6f;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 2, omega);
    }

    EXPECT_FALSE(std::isnan(st.across[0])) << "flow_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[0])) << "flow_out is Inf - SOR diverged";
    EXPECT_NEAR(st.across[0], expected_p, 1.0f)
        << "flow_out should converge to gravity head pressure";
}

// =============================================================================
// Different Densities
// =============================================================================

TEST(FuelTankRegression, DifferentDensity_HigherPressure) {
    // Heavier fuel → higher gravity head
    auto comp_light = make_tank(1000.0f, 1000.0f, 0.78f);
    auto comp_heavy = make_tank(1000.0f, 1000.0f, 1.0f);
    auto st_light = make_state();
    auto st_heavy = make_state();

    comp_light.solve_hydraulic(st_light, 1.0f / 5.0f);
    comp_heavy.solve_hydraulic(st_heavy, 1.0f / 5.0f);

    EXPECT_GT(st_heavy.through[0], st_light.through[0])
        << "Heavier fuel should produce more pressure";
}
