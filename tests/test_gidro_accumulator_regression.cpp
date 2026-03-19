/// Regression tests for GidroAccumulator component.
///
/// Tests verify:
/// - Boyle's law gas pressure computation
/// - Norton self-correction on p_out (prevents SOR divergence)
/// - stamp_two_port coupling between p_in and p_out
/// - Gas volume state update (compression/expansion)
/// - SOR convergence to equilibrium pressure
/// - pre_load() clamping

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static GidroAccumulator<JitProvider> make_accumulator(
    float precharge = 50.0f, float vol = 10.0f, float gas_vol = 10.0f
) {
    GidroAccumulator<JitProvider> comp;
    comp.precharge_pressure = precharge;
    comp.volume = vol;
    comp.gas_volume = gas_vol;
    comp.provider.indices[PortNames::p_in]  = 0;
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
// Gas Pressure (Boyle's Law)
// =============================================================================

TEST(GidroAccumulatorRegression, GasPressure_BoyleLaw_FullVolume) {
    // At full gas volume, gas pressure = precharge_pressure
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    auto st = make_state();
    st.across[0] = 50.0f;  // p_in = precharge
    st.across[1] = 0.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // p_gas = 50 * 10 / 10 = 50
    // Norton source on p_out: (50 - 0) * 1 = 50
    // Plus stamp_two_port coupling contribution
    EXPECT_GT(st.through[1], 0.0f)
        << "Through on p_out should be positive when p_gas > p_out";
    EXPECT_GT(st.conductance[1], 0.0f)
        << "Conductance on p_out should be stamped";
}

TEST(GidroAccumulatorRegression, GasPressure_BoyleLaw_CompressedGas) {
    // When gas is compressed to half volume, pressure doubles
    auto comp = make_accumulator(50.0f, 10.0f, 5.0f);
    auto st = make_state();
    st.across[0] = 100.0f;
    st.across[1] = 90.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // p_gas = 50 * 10 / 5 = 100
    // Norton on p_out: (100 - 90) * 1 = 10 (plus coupling)
    EXPECT_GT(st.through[1], 0.0f)
        << "Through should push p_out toward p_gas=100";
}

// =============================================================================
// Norton Self-Correction
// =============================================================================

TEST(GidroAccumulatorRegression, NortonResidual_SelfCorrection) {
    // When p_out is already at gas pressure, Norton contribution should be small
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    auto st = make_state();
    st.across[0] = 50.0f;
    st.across[1] = 50.0f;  // at equilibrium

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // Norton source: (50 - 50) * 1 = 0
    // stamp_two_port: (50 - 50) * g_coupling = 0
    // Only parasitic contributions
    EXPECT_NEAR(st.through[1], 0.0f, 1.0f)
        << "At equilibrium, through[p_out] should be near zero";
}

TEST(GidroAccumulatorRegression, NortonResidual_NegativeWhenOverpressure) {
    // When p_out > p_gas, Norton should pull it down
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    auto st = make_state();
    st.across[0] = 30.0f;   // p_in low
    st.across[1] = 100.0f;  // p_out much higher than gas pressure (50)

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // Norton: (50 - 100) * 1 = -50 (pulls down)
    // stamp_two_port: (30 - 100) * g_coupling = -700 (also pulls down on idx2=p_out)
    // Wait - stamp_two_port: through[idx2] -= i where i = (across[idx2] - across[idx1]) * g
    // i = (100 - 30) * 10 = 700, through[1] -= 700 = -700
    // Total through[1] = -700 + (-50) = -750
    EXPECT_LT(st.through[1], 0.0f)
        << "When p_out > p_gas AND p_in, through should be negative";
}

// =============================================================================
// Coupling (stamp_two_port)
// =============================================================================

TEST(GidroAccumulatorRegression, Coupling_StampsTwoPort) {
    // Both p_in and p_out should have conductance stamped
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    auto st = make_state();
    st.across[0] = 100.0f;
    st.across[1] = 80.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // g_coupling = 10.0 + g_gas = 1.0 on p_out
    EXPECT_GE(st.conductance[0], 10.0f)
        << "p_in should have coupling conductance stamped";
    EXPECT_GE(st.conductance[1], 11.0f)
        << "p_out should have coupling + gas conductance stamped";
}

// =============================================================================
// Gas Volume State Update
// =============================================================================

TEST(GidroAccumulatorRegression, GasVolume_CompressesWhenHighPressure) {
    // When p_in > p_gas, gas_volume should decrease (fluid enters)
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    float initial_gas_vol = comp.gas_volume;
    auto st = make_state();
    st.across[0] = 200.0f;  // p_in >> precharge
    st.across[1] = 0.0f;
    float dt = 1.0f / 5.0f;

    comp.solve_hydraulic(st, dt);
    comp.post_step(st, dt);  // gas_volume update moved to post_step

    EXPECT_LT(comp.gas_volume, initial_gas_vol)
        << "Gas should compress (volume decreases) when system pressure > precharge";
}

TEST(GidroAccumulatorRegression, GasVolume_ExpandsWhenLowPressure) {
    // When p_in < p_gas, gas_volume should increase (fluid exits)
    auto comp = make_accumulator(50.0f, 10.0f, 5.0f);
    float initial_gas_vol = comp.gas_volume;
    auto st = make_state();
    st.across[0] = 10.0f;   // p_in << p_gas (p_gas = 50*10/5 = 100)
    st.across[1] = 0.0f;
    float dt = 1.0f / 5.0f;

    comp.solve_hydraulic(st, dt);
    comp.post_step(st, dt);  // gas_volume update moved to post_step

    EXPECT_GT(comp.gas_volume, initial_gas_vol)
        << "Gas should expand (volume increases) when system pressure < gas pressure";
}

TEST(GidroAccumulatorRegression, GasVolume_ClampedToValidRange) {
    // gas_volume should never go below 0.1 or above volume
    auto comp = make_accumulator(50.0f, 10.0f, 0.2f);
    auto st = make_state();
    st.across[0] = 1e6f;  // enormous pressure to try to compress gas completely
    float dt = 100.0f;     // huge timestep

    comp.solve_hydraulic(st, dt);
    comp.post_step(st, dt);  // gas_volume update moved to post_step

    EXPECT_GE(comp.gas_volume, 0.1f)
        << "Gas volume should be clamped to minimum 0.1";
    EXPECT_LE(comp.gas_volume, comp.volume)
        << "Gas volume should not exceed total volume";
}

// =============================================================================
// pre_load()
// =============================================================================

TEST(GidroAccumulatorRegression, PreLoad_ClampsGasVolume) {
    auto comp = make_accumulator(50.0f, 10.0f, 20.0f);  // gas_vol > volume
    comp.pre_load();
    EXPECT_LE(comp.gas_volume, comp.volume)
        << "pre_load should clamp gas_volume to [0.1, volume]";

    comp.gas_volume = -5.0f;
    comp.pre_load();
    EXPECT_GE(comp.gas_volume, 0.1f)
        << "pre_load should clamp gas_volume >= 0.1";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(GidroAccumulatorRegression, SOR_ConvergesToEquilibrium) {
    auto comp = make_accumulator(50.0f, 10.0f, 10.0f);
    auto st = make_state(2);
    st.across[0] = 100.0f;  // high system pressure
    st.across[1] = 0.0f;    // p_out starts at 0

    const float omega = SOR::OMEGA;

    for (int step = 0; step < 200; ++step) {
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.conductance[0] = 1e-6f;
        st.conductance[1] = 1e-6f;

        // Fix p_in at 100
        float g_fix = 1e6f;
        st.conductance[0] += g_fix;
        st.through[0] += (100.0f - st.across[0]) * g_fix;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];

        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 2, omega);
    }

    EXPECT_FALSE(std::isnan(st.across[1])) << "p_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[1])) << "p_out is Inf - SOR diverged";
    EXPECT_GT(st.across[1], 0.0f)
        << "p_out should have risen from 0 toward equilibrium";
    EXPECT_LT(st.across[1], 200.0f)
        << "p_out should not wildly overshoot";
}
