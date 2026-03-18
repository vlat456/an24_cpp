/// Regression tests for [BUG-Radiator]: inverted heat flow direction.
///
/// The old Radiator::solve_thermal() computed:
///   delta = heat_in - heat_out
///   through[heat_in]  += delta * g   (WRONG: positive feedback on hot node)
///   through[heat_out] -= delta * g   (WRONG: negative feedback on cold node)
///
/// This caused the hot side to get hotter and cold side to get colder —
/// the exact opposite of a heat exchanger. The fix uses stamp_two_port()
/// which correctly transfers heat from hot to cold.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static Radiator<JitProvider> make_radiator(float capacity = 100.0f) {
    Radiator<JitProvider> comp;
    comp.cooling_capacity = capacity;
    comp.provider.indices[PortNames::heat_in] = 0;
    comp.provider.indices[PortNames::heat_out] = 1;
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
// Core Regression: Heat flows from hot to cold
// =============================================================================

TEST(RadiatorRegression, HeatFlowsFromHotToCold) {
    // Setup: heat_in = 100°C (hot), heat_out = 20°C (cold)
    auto comp = make_radiator(100.0f);
    auto st = make_state();
    st.across[0] = 100.0f;  // heat_in: hot side
    st.across[1] = 20.0f;   // heat_out: cold side

    comp.solve_thermal(st, 1.0f);

    // Heat should flow from hot (idx 0) to cold (idx 1):
    //   through[0] < 0 → pulls hot side DOWN (cooling it)
    //   through[1] > 0 → pulls cold side UP (warming it)
    // stamp_two_port computes: i = (across[idx2] - across[idx1]) * g
    //   i = (20 - 100) * 100 = -8000
    //   through[0] += i = -8000  (cools hot side)
    //   through[1] -= i = +8000  (warms cold side)
    EXPECT_LT(st.through[0], 0.0f) << "Hot side should lose heat (negative through)";
    EXPECT_GT(st.through[1], 0.0f) << "Cold side should gain heat (positive through)";
}

TEST(RadiatorRegression, HeatFlowMagnitudeIsCorrect) {
    auto comp = make_radiator(50.0f);
    auto st = make_state();
    st.across[0] = 80.0f;   // hot
    st.across[1] = 30.0f;   // cold

    comp.solve_thermal(st, 1.0f);

    // Expected: i = (30 - 80) * 50 = -2500
    float expected_i = (30.0f - 80.0f) * 50.0f;
    EXPECT_FLOAT_EQ(st.through[0], expected_i);
    EXPECT_FLOAT_EQ(st.through[1], -expected_i);
}

TEST(RadiatorRegression, EqualTemperatures_NoHeatFlow) {
    auto comp = make_radiator(100.0f);
    auto st = make_state();
    st.across[0] = 50.0f;
    st.across[1] = 50.0f;

    comp.solve_thermal(st, 1.0f);

    EXPECT_FLOAT_EQ(st.through[0], 0.0f);
    EXPECT_FLOAT_EQ(st.through[1], 0.0f);
    // Conductance still stamped
    EXPECT_FLOAT_EQ(st.conductance[0], 100.0f);
    EXPECT_FLOAT_EQ(st.conductance[1], 100.0f);
}

TEST(RadiatorRegression, ConductanceStampedSymmetrically) {
    auto comp = make_radiator(200.0f);
    auto st = make_state();
    st.across[0] = 100.0f;
    st.across[1] = 0.0f;

    comp.solve_thermal(st, 1.0f);

    EXPECT_FLOAT_EQ(st.conductance[0], 200.0f);
    EXPECT_FLOAT_EQ(st.conductance[1], 200.0f);
}

TEST(RadiatorRegression, ReverseTemperatureGradient_FlowReverses) {
    // cold on heat_in, hot on heat_out
    auto comp = make_radiator(100.0f);
    auto st = make_state();
    st.across[0] = 10.0f;   // heat_in: cold
    st.across[1] = 90.0f;   // heat_out: hot

    comp.solve_thermal(st, 1.0f);

    // Heat should flow from hot (idx 1) to cold (idx 0):
    //   through[0] > 0 (warms cold side)
    //   through[1] < 0 (cools hot side)
    EXPECT_GT(st.through[0], 0.0f) << "Cold side should gain heat";
    EXPECT_LT(st.through[1], 0.0f) << "Hot side should lose heat";
}

TEST(RadiatorRegression, ConservativeHeatTransfer) {
    // Energy conservation: total through contribution sums to zero
    auto comp = make_radiator(75.0f);
    auto st = make_state();
    st.across[0] = 120.0f;
    st.across[1] = 45.0f;

    comp.solve_thermal(st, 1.0f);

    EXPECT_FLOAT_EQ(st.through[0] + st.through[1], 0.0f)
        << "Heat transfer must be conservative (sum = 0)";
}

// =============================================================================
// SOR Convergence: Radiator should equilibrate temperatures
// =============================================================================

TEST(RadiatorRegression, SOR_ConvergesToEquilibrium) {
    // Run multiple SOR iterations: two nodes connected by a radiator
    // should converge toward each other. Use balanced conductance ratio
    // so convergence happens in reasonable iteration count.
    auto comp = make_radiator(1.0f);
    auto st = make_state(2);
    float T_hot_init = 100.0f;
    float T_cold_init = 0.0f;
    st.across[0] = T_hot_init;
    st.across[1] = T_cold_init;

    const float omega = 1.0f;

    for (int step = 0; step < 500; ++step) {
        // Clear stamps each step (like the real simulation)
        st.through[0] = 0.0f;
        st.through[1] = 0.0f;
        st.conductance[0] = 0.5f;  // meaningful self-conductance
        st.conductance[1] = 0.5f;

        comp.solve_thermal(st, 1.0f);

        // Precompute inv_conductance
        st.inv_conductance[0] = 1.0f / st.conductance[0];
        st.inv_conductance[1] = 1.0f / st.conductance[1];

        // SOR update (omega=1.0)
        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 2, omega);
    }

    // Both nodes should converge toward 50°C (midpoint).
    // With g_radiator=1.0 and g_self=0.5, equilibrium is exactly
    // (g_rad * T_other + 0) / (g_rad + g_self) which converges to 50°C.
    EXPECT_NEAR(st.across[0], 50.0f, 5.0f)
        << "Hot side should converge toward equilibrium";
    EXPECT_NEAR(st.across[1], 50.0f, 5.0f)
        << "Cold side should converge toward equilibrium";

    // Critical: the hot side MUST be lower than its initial temp (cooled down)
    EXPECT_LT(st.across[0], T_hot_init)
        << "Hot side should have cooled (not heated up as with the old bug)";
    EXPECT_GT(st.across[1], T_cold_init)
        << "Cold side should have warmed (not cooled as with the old bug)";

    // Sanity
    EXPECT_FALSE(std::isnan(st.across[0])) << "Hot side is NaN";
    EXPECT_FALSE(std::isnan(st.across[1])) << "Cold side is NaN";
    EXPECT_FALSE(std::isinf(st.across[0])) << "Hot side is Inf";
    EXPECT_FALSE(std::isinf(st.across[1])) << "Cold side is Inf";
}
