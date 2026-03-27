/// Regression tests for [BUG-24]: Switch::solve_electrical() missing downstream
/// current forwarding term.
///
/// The old code computed:
///   through[v_in] -= v_in * g
/// instead of the correct two-port Norton stamp:
///   through[v_in] += downstream_I - v_in * g
///
/// Additionally, finalize_step() was not capturing downstream_I, so even if
/// solve_electrical used it, the value would always be zero.
///
/// When a load (e.g., a lamp or motor) is connected downstream of a Switch,
/// the load's current contribution (downstream_I) must be relayed upstream
/// through the switch. Without this term, the switch acts as a dead-end
/// that sinks voltage but never forwards the actual load current, causing
/// incorrect voltage distribution in the SOR solver.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

/// Port layout: [0]=control, [1]=state, [2]=v_in, [3]=v_out
static Switch<JitProvider> make_switch() {
    Switch<JitProvider> comp;
    comp.provider.indices[PortNames::control] = 0;
    comp.provider.indices[PortNames::state]   = 1;
    comp.provider.indices[PortNames::v_in]    = 2;
    comp.provider.indices[PortNames::v_out]   = 3;
    return comp;
}

static SimulationState make_state(size_t n = 6) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    st.inv_conductance.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Core Regression: downstream_I forwarding
// =============================================================================

TEST(SwitchRegression, ClosedSwitch_ForwardsDownstreamCurrent) {
    // A closed switch must relay downstream_I to v_in's through.
    // This is the core BUG-24 fix.
    auto comp = make_switch();
    comp.closed = true;
    comp.downstream_g = 0.5f;
    comp.downstream_I = 2.0f;  // downstream load demands 2A of Norton current

    auto st = make_state();
    st.across[2] = 28.0f;   // v_in = 28V
    st.across[3] = 28.0f;   // v_out = 28V

    comp.solve_electrical(st, 1.0f / 60.0f);

    // Expected: through[v_in] += downstream_I - v_in * g = 2.0 - 28.0 * 0.5 = -12.0
    float expected_through = 2.0f - 28.0f * 0.5f;
    EXPECT_FLOAT_EQ(st.through[2], expected_through)
        << "through[v_in] must include downstream_I term, not just -v*g";
}

TEST(SwitchRegression, ClosedSwitch_ConductanceStamped) {
    auto comp = make_switch();
    comp.closed = true;
    comp.downstream_g = 0.5f;

    auto st = make_state();
    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.conductance[2], 0.5f)
        << "closed switch should stamp downstream_g onto v_in";
}

TEST(SwitchRegression, OpenSwitch_ZeroContribution) {
    auto comp = make_switch();
    comp.closed = false;
    comp.downstream_g = 0.5f;
    comp.downstream_I = 2.0f;

    auto st = make_state();
    st.across[2] = 28.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.through[2], 0.0f)
        << "open switch should contribute nothing to through";
    EXPECT_FLOAT_EQ(st.conductance[2], 0.0f)
        << "open switch should contribute nothing to conductance";
}

// =============================================================================
// finalize_step: downstream_I capture
// =============================================================================

TEST(SwitchRegression, FinalizePhase_CapturesDownstreamI) {
    // After the solver step, finalize_step should capture the downstream
    // through + v_out_old * conductance as downstream_I for the next step.
    auto comp = make_switch();
    comp.closed = true;
    comp.last_control = 1.0f;  // already at 1 so no toggle occurs
    comp.v_out_old = 26.0f;    // v_out at start of solve_electrical

    auto st = make_state();
    st.across[0] = 1.0f;   // control = 1 (keep closed, same as last_control)
    st.across[2] = 28.0f;  // v_in
    st.across[3] = 27.0f;  // v_out (current)
    st.through[3] = 1.5f;  // downstream load's through contribution
    st.conductance[3] = 0.5f;

    comp.commit_control(st, 1.0f / 60.0f);

    // downstream_I = through[v_out] + v_out_old * conductance[v_out]
    float expected_I = 1.5f + 26.0f * 0.5f;  // 14.5
    EXPECT_FLOAT_EQ(comp.downstream_I, expected_I)
        << "finalize_step must capture downstream_I = through[v_out] + v_out_old * g";
    EXPECT_FLOAT_EQ(comp.downstream_g, 0.5f)
        << "finalize_step must capture downstream_g from conductance[v_out]";
}

TEST(SwitchRegression, FinalizePhase_OpenSwitch_ZerosDownstreamI) {
    auto comp = make_switch();
    comp.closed = true;
    comp.downstream_I = 10.0f;  // leftover from previous step

    auto st = make_state();
    st.across[0] = 0.0f;  // control = 0
    comp.last_control = 1.0f;  // was 1 → toggled to open

    comp.commit_control(st, 1.0f / 60.0f);

    EXPECT_FALSE(comp.closed);
    EXPECT_FLOAT_EQ(comp.downstream_I, 0.0f)
        << "open switch must zero downstream_I";
    EXPECT_FLOAT_EQ(comp.downstream_g, 0.0f)
        << "open switch must zero downstream_g";
}

// =============================================================================
// SOR Convergence: Switch between Battery (28V) and Load (resistor)
// =============================================================================

TEST(SwitchRegression, SOR_ConvergesWithLoad) {
    // Setup: signal 0=control, 1=state, 2=v_in (battery side), 3=v_out (load side)
    auto comp = make_switch();
    comp.closed = true;
    comp.last_control = 1.0f;  // already at 1 so no toggle occurs

    auto st = make_state(4);
    st.across[0] = 1.0f;   // control = on (matches last_control, no toggle)
    st.across[2] = 0.0f;   // v_in starts at 0
    st.across[3] = 0.0f;   // v_out starts at 0

    const float omega = SOR::OMEGA;
    const float v_battery = 28.0f;
    const float g_battery = 100.0f;    // stiff battery source
    const float g_load    = 1.0f;      // 1 Siemens load (1 ohm resistor)

    for (int step = 0; step < 200; ++step) {
        // Reset accumulators
        for (size_t i = 0; i < 4; ++i) {
            st.through[i] = 0.0f;
            st.conductance[i] = 1e-6f;  // parasitic
        }

        // Battery Norton source on v_in: stamps (v_battery - v_in) * g_battery
        st.conductance[2] += g_battery;
        st.through[2] += (v_battery - st.across[2]) * g_battery;

        // Load on v_out: stamps -v_out * g_load
        st.conductance[3] += g_load;
        st.through[3] += -st.across[3] * g_load;

        // Switch electrical stamp
        comp.solve_electrical(st, 1.0f / 60.0f);

        // SOR update
        for (size_t i = 0; i < 4; ++i) {
            st.inv_conductance[i] = 1.0f / st.conductance[i];
        }
        solve_sor_iteration(st.across.data(), st.through.data(),
                           st.inv_conductance.data(), 4, omega);

        // finalize_step: capture downstream for next iteration
        comp.commit_control(st, 1.0f / 60.0f);
    }

    // v_in and v_out should both converge to ~28V (switch is ideal conductor)
    EXPECT_NEAR(st.across[2], v_battery, 1.0f)
        << "v_in should converge to battery voltage";
    EXPECT_NEAR(st.across[3], v_battery, 1.0f)
        << "v_out should converge to battery voltage through closed switch";

    EXPECT_FALSE(std::isnan(st.across[2])) << "v_in is NaN - SOR diverged";
    EXPECT_FALSE(std::isnan(st.across[3])) << "v_out is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[2])) << "v_in is Inf - SOR diverged";
    EXPECT_FALSE(std::isinf(st.across[3])) << "v_out is Inf - SOR diverged";
}

// =============================================================================
// v_out_old capture (needed for correct downstream_I computation)
// =============================================================================

TEST(SwitchRegression, SolveElectrical_CapturesVOutOld) {
    // solve_electrical must snapshot v_out_old before the SOR update
    // so finalize_step can compute downstream_I correctly.
    auto comp = make_switch();
    comp.closed = true;
    comp.downstream_g = 0.1f;

    auto st = make_state();
    st.across[3] = 25.5f;  // v_out before solve

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(comp.v_out_old, 25.5f)
        << "solve_electrical must capture v_out_old at the start";
}
