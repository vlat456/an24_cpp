#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

/// Port layout: [0]=v_in, [1]=v_out, [2]=i_out
static CurrentSense<JitProvider> make_current_sense(float g = 1000.0f) {
    CurrentSense<JitProvider> comp;
    comp.conductance = g;
    comp.provider.indices[PortNames::v_in]  = 0;
    comp.provider.indices[PortNames::v_out] = 1;
    comp.provider.indices[PortNames::i_out] = 2;
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
// Basic Stamping Tests
// =============================================================================

TEST(CurrentSense, ConductanceStampedOnBothPorts) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.conductance[0], 1000.0f)
        << "v_in should receive conductance stamp";
    EXPECT_FLOAT_EQ(st.conductance[1], 1000.0f)
        << "v_out should receive conductance stamp";
}

TEST(CurrentSense, NoConductanceOnOutputPort) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.conductance[2], 0.0f)
        << "i_out should not receive any conductance stamp";
}

TEST(CurrentSense, ZeroVoltage_ZeroCurrent_ObserverPhase) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    // both v_in and v_out = 0 => no current
    st.across[0] = 0.0f;
    st.across[1] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.observe_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f)
        << "i_out should be zero when no voltage difference";
}

TEST(CurrentSense, EqualVoltage_ZeroCurrent_ObserverPhase) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 28.0f;  // v_out

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.observe_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f)
        << "i_out should be zero when v_in == v_out";
}

// =============================================================================
// Current Measurement Tests
// =============================================================================

TEST(CurrentSense, PositiveCurrentFlow_ObserverPhase) {
    // v_in > v_out => positive current (conventional direction: in -> out)
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 27.9f;  // v_out (slight drop across ammeter)

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.observe_electrical(st, 1.0f / 60.0f);

    float expected_i = (28.0f - 27.9f) * 1000.0f; // 100 A
    EXPECT_FLOAT_EQ(st.across[2], expected_i)
        << "i_out = (v_in - v_out) * conductance";
}

TEST(CurrentSense, NegativeCurrentFlow_ObserverPhase) {
    // v_out > v_in => negative current (reverse flow)
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 27.0f;  // v_in
    st.across[1] = 28.0f;  // v_out

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.observe_electrical(st, 1.0f / 60.0f);

    float expected_i = (27.0f - 28.0f) * 1000.0f; // -1000 A
    EXPECT_FLOAT_EQ(st.across[2], expected_i)
        << "i_out should be negative when v_out > v_in";
}

TEST(CurrentSense, CustomConductance_ObserverPhase) {
    auto comp = make_current_sense(500.0f);
    auto st = make_state();
    st.across[0] = 10.0f;
    st.across[1] = 9.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.observe_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], (10.0f - 9.0f) * 500.0f)
        << "i_out should use the configured conductance";
    EXPECT_FLOAT_EQ(st.conductance[0], 500.0f);
    EXPECT_FLOAT_EQ(st.conductance[1], 500.0f);
}

// =============================================================================
// KCL Conservation Tests
// =============================================================================

TEST(CurrentSense, EnergyConservation_ThroughSumsToZero) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 27.5f;  // v_out

    comp.solve_electrical(st, 1.0f / 60.0f);

    // stamp_two_port guarantees through[v_in] + through[v_out] == 0 (KCL)
    EXPECT_NEAR(st.through[0] + st.through[1], 0.0f, 1e-4f)
        << "KCL: currents entering and leaving the ammeter must cancel";
}

TEST(CurrentSense, ThroughSignConvention) {
    // When v_in > v_out, stamp_two_port(v_out, v_in, g) means:
    // i = (across[v_in] - across[v_out]) * g  (positive)
    // through[v_out] += i (pushes v_out up toward v_in)
    // through[v_in]  -= i (pulls v_in down toward v_out)
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;  // v_in
    st.across[1] = 27.0f;  // v_out

    comp.solve_electrical(st, 1.0f / 60.0f);

    // through[v_out] should be positive (SOR will push v_out up)
    EXPECT_GT(st.through[1], 0.0f)
        << "through[v_out] should be positive to pull v_out toward v_in";
    // through[v_in] should be negative (SOR will pull v_in down)
    EXPECT_LT(st.through[0], 0.0f)
        << "through[v_in] should be negative to pull v_in toward v_out";
}

// =============================================================================
// SOR Convergence: CurrentSense in series between Battery and Load
// =============================================================================
//
// NOTE: The CurrentSense G=1000 two-port is very stiff. Isolated SOR with
// omega > 1 can diverge when the conductance ratio is extreme (G_sense >>
// G_battery). In a full system, many components damp the system. For unit
// tests we use omega=1.0 (Gauss-Seidel) and only iterate over the two
// electrical nodes [0..2). The i_out signal (index 2) is a derived output,
// not a nodal variable, so it is excluded from SOR.
// =============================================================================

/// Helper: run SOR convergence loop for CurrentSense between battery and load.
/// Returns the final SimulationState after convergence.
static SimulationState run_current_sense_sor(
    float g_sense, float v_battery, float g_battery, float g_load, int steps = 500)
{
    CurrentSense<JitProvider> comp;
    comp.conductance = g_sense;
    comp.provider.indices[PortNames::v_in]  = 0;
    comp.provider.indices[PortNames::v_out] = 1;
    comp.provider.indices[PortNames::i_out] = 2;

    SimulationState st;
    const size_t n_electrical = 2;  // only v_in and v_out are nodal variables
    const size_t n_total = 3;       // + i_out output signal
    st.across.resize(n_total, 0.0f);
    st.through.resize(n_total, 0.0f);
    st.conductance.resize(n_total, 0.0f);
    st.inv_conductance.resize(n_total, 0.0f);

    const float omega = 1.0f;  // Gauss-Seidel for isolated stiff two-port

    for (int step = 0; step < steps; ++step) {
        // Reset accumulators
        for (size_t i = 0; i < n_total; ++i) {
            st.through[i] = 0.0f;
            st.conductance[i] = 1e-6f;  // parasitic
        }

        // Battery Norton source on v_in
        st.conductance[0] += g_battery;
        st.through[0] += (v_battery - st.across[0]) * g_battery;

        // Load on v_out: grounded resistor stamps -v_out * g_load
        if (g_load > 0.0f) {
            st.conductance[1] += g_load;
            st.through[1] += -st.across[1] * g_load;
        }

        // CurrentSense electrical stamp (stamps v_in <-> v_out coupling)
        comp.solve_electrical(st, 1.0f / 60.0f);

        // SOR update only over electrical nodes (exclude i_out output signal)
        for (size_t i = 0; i < n_electrical; ++i) {
            st.inv_conductance[i] = 1.0f / st.conductance[i];
        }
        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), n_electrical, omega);

        // Observer phase writes measured current from converged nodal voltages.
        comp.observe_electrical(st, 1.0f / 60.0f);
    }

    return st;
}

TEST(CurrentSense, ElectricalPhaseDoesNotWriteIOut) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 27.0f;
    st.across[2] = 123.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 123.0f)
        << "solve_electrical should not update i_out; observer phase owns measured output";
}

TEST(CurrentSense, SOR_ConvergesWithBatteryAndLoad) {
    // Circuit: Battery(28V) --[v_in]-- CurrentSense --[v_out]-- Load(1 ohm)
    // Expected: v_in ~ v_out ~ 28V, i_out ~ 28A
    auto st = run_current_sense_sor(1000.0f, 28.0f, 200.0f, 1.0f);

    EXPECT_NEAR(st.across[0], 28.0f, 0.5f)
        << "v_in should converge to battery voltage";
    EXPECT_NEAR(st.across[1], 28.0f, 0.5f)
        << "v_out should be near v_in (high conductance passthrough)";

    // Measured current: i_out ~ V_load * G_load
    float expected_current = st.across[1] * 1.0f;
    EXPECT_NEAR(st.across[2], expected_current, 1.0f)
        << "i_out should measure approximately the load current";

    // Divergence guards
    EXPECT_FALSE(std::isnan(st.across[0])) << "v_in is NaN - SOR diverged";
    EXPECT_FALSE(std::isnan(st.across[1])) << "v_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isnan(st.across[2])) << "i_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[0])) << "v_in is Inf - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[1])) << "v_out is Inf - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[2])) << "i_out is Inf - SOR diverged";
}

TEST(CurrentSense, SOR_ConvergesWithHeavyLoad) {
    // Heavy load: 10 Siemens (0.1 ohm) - high current measurement
    auto st = run_current_sense_sor(1000.0f, 28.0f, 200.0f, 10.0f);

    EXPECT_NEAR(st.across[1], 28.0f, 2.0f)
        << "v_out should be near battery voltage even with heavy load";

    float expected_current = st.across[1] * 10.0f;
    EXPECT_NEAR(st.across[2], expected_current, 5.0f)
        << "i_out should measure the heavy load current";

    EXPECT_GT(st.across[2], 0.0f)
        << "i_out should be positive with normal current flow direction";

    EXPECT_FALSE(std::isnan(st.across[2])) << "i_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[2])) << "i_out is Inf - SOR diverged";
}

TEST(CurrentSense, SOR_ZeroCurrent_NoLoad) {
    // No load: only parasitic conductance on v_out
    auto st = run_current_sense_sor(1000.0f, 28.0f, 200.0f, 0.0f);

    EXPECT_NEAR(st.across[0], 28.0f, 0.5f)
        << "v_in should converge to battery voltage";
    EXPECT_NEAR(st.across[1], 28.0f, 0.5f)
        << "v_out should also converge to battery voltage with no load";

    // Current should be near zero (only parasitic leakage)
    EXPECT_NEAR(st.across[2], 0.0f, 1.0f)
        << "i_out should be near zero with no load";

    EXPECT_FALSE(std::isnan(st.across[0])) << "v_in is NaN";
    EXPECT_FALSE(std::isnan(st.across[1])) << "v_out is NaN";
    EXPECT_FALSE(std::isnan(st.across[2])) << "i_out is NaN";
}

// =============================================================================
// Voltage Drop Verification (ammeter should have negligible voltage drop)
// =============================================================================

TEST(CurrentSense, SOR_NegligibleVoltageDrop) {
    // V_drop = I / G. With G=1000 and I~28A, V_drop ~ 0.028V
    auto st = run_current_sense_sor(1000.0f, 28.0f, 200.0f, 1.0f);

    float v_drop = std::abs(st.across[0] - st.across[1]);
    EXPECT_LT(v_drop, 0.1f)
        << "Ammeter voltage drop should be negligible (<100mV) with G=1000";
}
