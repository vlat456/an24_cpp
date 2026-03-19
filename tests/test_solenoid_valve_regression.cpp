/// Regression tests for [BUG-SolenoidValve]: Disconnected port stamping.
///
/// The old SolenoidValve::solve_hydraulic() stamped independent conductances
/// on flow_in and flow_out without coupling them via stamp_two_port.
/// This meant the valve loaded each port to ground independently instead of
/// passing fluid through (connecting flow_in to flow_out).
///
/// The fix uses stamp_two_port() to create a proper conductance path between
/// the two ports, so pressure difference drives flow through the valve.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static SolenoidValve<JitProvider> make_valve(bool normally_closed = true) {
    SolenoidValve<JitProvider> comp;
    comp.normally_closed = normally_closed;
    comp.provider.indices[PortNames::ctrl] = 0;
    comp.provider.indices[PortNames::flow_in] = 1;
    comp.provider.indices[PortNames::flow_out] = 2;
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
// Core Regression: Two-port coupling when open
// =============================================================================

TEST(SolenoidValveRegression, Open_ThroughCoupling) {
    // When valve is open, through[flow_in] and through[flow_out] should
    // reflect pressure difference (two-port coupling)
    auto comp = make_valve(true);  // NC, open when ctrl > 12V
    auto st = make_state();
    st.across[0] = 28.0f;   // ctrl above threshold
    st.across[1] = 100.0f;  // flow_in pressure
    st.across[2] = 50.0f;   // flow_out pressure

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    // stamp_two_port: through[flow_in] = (V2-V1)*g, through[flow_out] = -(V2-V1)*g
    // V2=flow_out=50, V1=flow_in=100, g=100
    // through[flow_in] = (50-100)*100 = -5000  (pressure drops)
    // through[flow_out] = +5000  (pressure rises)
    EXPECT_LT(st.through[1], 0.0f)
        << "through[flow_in] should be negative (high-to-low pressure)";
    EXPECT_GT(st.through[2], 0.0f)
        << "through[flow_out] should be positive (receiving flow)";
}

TEST(SolenoidValveRegression, Open_ThroughAntisymmetric) {
    // through[flow_in] + through[flow_out] should be 0 (flow conservation)
    auto comp = make_valve(true);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 100.0f;
    st.across[2] = 50.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[1] + st.through[2], 0.0f, 1e-3f)
        << "Two-port coupling must conserve flow (sum of through = 0)";
}

TEST(SolenoidValveRegression, Open_EqualPressure_ZeroThrough) {
    // When both ports at same pressure, no flow through
    auto comp = make_valve(true);
    auto st = make_state();
    st.across[0] = 28.0f;   // open
    st.across[1] = 100.0f;  // same pressure
    st.across[2] = 100.0f;  // same pressure

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[1], 0.0f, 1e-3f)
        << "No flow when pressures are equal";
    EXPECT_NEAR(st.through[2], 0.0f, 1e-3f)
        << "No flow when pressures are equal";
}

// =============================================================================
// Closed valve: zero coupling
// =============================================================================

TEST(SolenoidValveRegression, Closed_NoCoupling) {
    // Normally-closed valve with no control signal: should be closed
    auto comp = make_valve(true);
    auto st = make_state();
    st.across[0] = 0.0f;    // ctrl below threshold
    st.across[1] = 100.0f;
    st.across[2] = 0.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[1], 0.0f, 1e-6f)
        << "Closed valve should have zero through on flow_in";
    EXPECT_NEAR(st.through[2], 0.0f, 1e-6f)
        << "Closed valve should have zero through on flow_out";
    EXPECT_NEAR(st.conductance[1], 0.0f, 1e-6f)
        << "Closed valve should have zero conductance on flow_in";
    EXPECT_NEAR(st.conductance[2], 0.0f, 1e-6f)
        << "Closed valve should have zero conductance on flow_out";
}

// =============================================================================
// Normally-open valve
// =============================================================================

TEST(SolenoidValveRegression, NormallyOpen_OpenWhenNoControl) {
    // Normally-open valve: open when ctrl < 12V, closed when ctrl > 12V
    auto comp = make_valve(false);  // normally open
    auto st = make_state();
    st.across[0] = 0.0f;    // no control = open for NO valve
    st.across[1] = 100.0f;
    st.across[2] = 50.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NE(st.through[1], 0.0f)
        << "Normally-open valve should have flow when unpowered";
}

TEST(SolenoidValveRegression, NormallyOpen_ClosedWhenPowered) {
    auto comp = make_valve(false);
    auto st = make_state();
    st.across[0] = 28.0f;  // powered = closed for NO valve
    st.across[1] = 100.0f;
    st.across[2] = 50.0f;

    comp.solve_hydraulic(st, 1.0f / 5.0f);

    EXPECT_NEAR(st.through[1], 0.0f, 1e-6f)
        << "Normally-open valve should be closed when powered";
}

// =============================================================================
// SOR Convergence
// =============================================================================

TEST(SolenoidValveRegression, SOR_EqualizePressure) {
    // Open valve between two nodes: SOR should equalize pressure.
    // Nodes need weak ground references to prevent floating (as in real circuits
    // where tanks, pumps, etc. anchor node pressures).
    auto comp = make_valve(true);
    auto st = make_state();
    st.across[0] = 28.0f;   // ctrl = open
    st.across[1] = 100.0f;  // high pressure
    st.across[2] = 0.0f;    // low pressure

    const float omega = SOR::OMEGA;

    for (int step = 0; step < 200; ++step) {
        st.through[1] = 0.0f;
        st.through[2] = 0.0f;
        st.conductance[1] = 1e-6f;
        st.conductance[2] = 1e-6f;

        // Ground references model reservoir/tank connections. Their conductance
        // must be comparable to valve conductance for SOR stability in this
        // isolated two-node test. In a full circuit, other components provide
        // this naturally.
        float g_ground = 100.0f;
        st.conductance[1] += g_ground;
        st.through[1] += (100.0f - st.across[1]) * g_ground;
        st.conductance[2] += g_ground;
        st.through[2] += (0.0f - st.across[2]) * g_ground;

        comp.solve_hydraulic(st, 1.0f / 5.0f);

        st.inv_conductance[1] = 1.0f / st.conductance[1];
        st.inv_conductance[2] = 1.0f / st.conductance[2];

        solve_sor_iteration(st.across.data() + 1, st.through.data() + 1,
                           st.inv_conductance.data() + 1, 2, omega);
    }

    // With ground references, nodes converge toward each other but not exactly
    // equal - the ground bias prevents full equalization. The key is that the
    // valve significantly reduces the pressure difference between the ports.
    float diff = std::abs(st.across[1] - st.across[2]);
    EXPECT_LT(diff, 50.0f)
        << "Open valve should significantly reduce pressure difference between ports";
    EXPECT_FALSE(std::isnan(st.across[1])) << "SOR diverged to NaN";
    EXPECT_FALSE(std::isinf(st.across[1])) << "SOR diverged to Inf";
}
