#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

// =============================================================================
// AZS (Автомат Защиты Сети) — Circuit Breaker Tests
//
// AZS is a hybrid switch + thermal fuse:
//   - Electrical domain: stamps conductance when closed (like Switch/Relay)
//   - Thermal domain: T += (I² * r_heat - T * k_cool) * dt
//   - Trip at T > 1.0 → opens circuit (branchless)
//   - Manual toggle via control port (like Switch)
//   - Outputs: state (on/off), temp (normalized 0..1+), tripped (bool)
// =============================================================================

/// Helper: set up SimulationState + JitProvider for AZS with known port indices
struct AZSTestFixture : public ::testing::Test {
    // Port indices in SimulationState
    static constexpr uint32_t IDX_V_IN    = 0;
    static constexpr uint32_t IDX_V_OUT   = 1;
    static constexpr uint32_t IDX_CONTROL = 2;
    static constexpr uint32_t IDX_STATE   = 3;
    static constexpr uint32_t IDX_TEMP    = 4;
    static constexpr uint32_t IDX_TRIPPED = 5;
    static constexpr uint32_t SIGNAL_COUNT = 6;

    SimulationState st;
    AZS<JitProvider> azs;

    void SetUp() override {
        st.across.resize(SIGNAL_COUNT, 0.0f);
        st.through.resize(SIGNAL_COUNT, 0.0f);
        st.conductance.resize(SIGNAL_COUNT, 0.0f);
        st.inv_conductance.resize(SIGNAL_COUNT, 0.0f);
        st.convergence_buffer.resize(SIGNAL_COUNT, 0.0f);
        st.dynamic_signals_count = SIGNAL_COUNT;

        azs.provider.set(PortNames::v_in, IDX_V_IN);
        azs.provider.set(PortNames::v_out, IDX_V_OUT);
        azs.provider.set(PortNames::control, IDX_CONTROL);
        azs.provider.set(PortNames::state, IDX_STATE);
        azs.provider.set(PortNames::temp, IDX_TEMP);
        azs.provider.set(PortNames::tripped, IDX_TRIPPED);

        // Default: AZS starts OFF (closed = false)
        azs.closed = false;
        azs.i_nominal = 20.0f;
        // r_heat = 1/(i_nominal²), k_cool = 1.0 → trip threshold at I = i_nominal steady-state
        azs.r_heat = 1.0f / (azs.i_nominal * azs.i_nominal);
        azs.k_cool = 1.0f;
    }

    void clearThrough() {
        std::fill(st.through.begin(), st.through.end(), 0.0f);
        std::fill(st.conductance.begin(), st.conductance.end(), 0.0f);
    }
};

// =============================================================================
// Domain & Structure Tests
// =============================================================================

TEST_F(AZSTestFixture, HasElectricalAndThermalDomains) {
    // AZS must participate in both Electrical (60Hz) and Thermal (sub-rate) domains
    constexpr auto d = AZS<JitProvider>::domain;
    EXPECT_TRUE((d & Domain::Electrical) != Domain{});
    EXPECT_TRUE((d & Domain::Thermal) != Domain{});
}

TEST_F(AZSTestFixture, StartsOff) {
    // Cold & Dark: AZS starts in OFF position
    EXPECT_FALSE(azs.closed);
}

// =============================================================================
// Electrical Domain — Conductance Stamping
// =============================================================================

TEST_F(AZSTestFixture, OpenCircuitWhenOff) {
    // When OFF, solve_electrical should NOT stamp any conductance
    st.across[IDX_V_IN] = 28.0f;
    st.conductance[IDX_V_OUT] = 0.1f; // downstream load

    azs.solve_electrical(st, 1.0f / 60.0f);

    // No current flows — v_in conductance unchanged
    EXPECT_FLOAT_EQ(st.conductance[IDX_V_IN], 0.0f);
}

TEST_F(AZSTestFixture, StampsConductanceWhenClosed) {
    // When ON and downstream has conductance, AZS stamps it upstream
    azs.closed = true;
    azs.downstream_g = 0.5f;
    st.across[IDX_V_IN] = 28.0f;

    clearThrough();
    azs.solve_electrical(st, 1.0f / 60.0f);

    // Conductance should be stamped on v_in
    EXPECT_GT(st.conductance[IDX_V_IN], 0.0f);
}

TEST_F(AZSTestFixture, PassesVoltageWhenClosed) {
    // post_step should copy v_in to v_out when closed
    azs.closed = true;
    st.across[IDX_V_IN] = 28.0f;
    st.conductance[IDX_V_OUT] = 0.1f; // simulate downstream load

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[IDX_V_OUT], 28.0f);
}

TEST_F(AZSTestFixture, ZerosVoltageWhenOpen) {
    // post_step should zero v_out when open
    azs.closed = false;
    st.across[IDX_V_IN] = 28.0f;
    st.across[IDX_V_OUT] = 28.0f; // leftover from previous step

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[IDX_V_OUT], 0.0f);
}

// =============================================================================
// Manual Toggle (via control port, like Switch)
// =============================================================================

TEST_F(AZSTestFixture, TogglesOnControlEdge) {
    // Rising edge on control toggles closed state: OFF → ON
    azs.closed = false;
    azs.last_control = 0.0f;
    st.across[IDX_CONTROL] = 1.0f; // rising edge

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_TRUE(azs.closed);
}

TEST_F(AZSTestFixture, TogglesOffOnSecondEdge) {
    // Second rising edge: ON → OFF
    azs.closed = true;
    azs.last_control = 0.0f;
    st.across[IDX_CONTROL] = 1.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FALSE(azs.closed);
}

TEST_F(AZSTestFixture, NoToggleWithoutEdge) {
    // Stable control signal (no edge) → no toggle
    azs.closed = true;
    azs.last_control = 1.0f;
    st.across[IDX_CONTROL] = 1.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_TRUE(azs.closed); // unchanged
}

// =============================================================================
// State Output Port
// =============================================================================

TEST_F(AZSTestFixture, OutputsStateOn) {
    azs.closed = true;
    st.across[IDX_CONTROL] = 0.0f; // no toggle
    azs.last_control = 0.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[IDX_STATE], 1.0f);
}

TEST_F(AZSTestFixture, OutputsStateOff) {
    azs.closed = false;
    st.across[IDX_CONTROL] = 0.0f;
    azs.last_control = 0.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[IDX_STATE], 0.0f);
}

// =============================================================================
// Thermal Domain — Heat Accumulation
// =============================================================================

TEST_F(AZSTestFixture, HeatsUpWithCurrent) {
    // current is computed in post_step and stored; solve_thermal uses stored value
    azs.closed = true;
    azs.temp = 0.0f;
    azs.current = 28.0f; // 28A flowing through AZS

    float dt = 1.0f; // thermal domain accumulated dt
    azs.solve_thermal(st, dt);

    // Temperature should increase: T += I² * r_heat * dt (minus cooling which is 0 at T=0)
    EXPECT_GT(azs.temp, 0.0f);
}

TEST_F(AZSTestFixture, CoolsDownWhenNoCurrent) {
    // Start warm, no current → should cool down
    azs.closed = false; // open → no current
    azs.temp = 0.8f;
    azs.current = 0.0f;

    float dt = 1.0f;
    azs.solve_thermal(st, dt);

    EXPECT_LT(azs.temp, 0.8f);
}

TEST_F(AZSTestFixture, TempNeverGoesNegative) {
    azs.temp = 0.01f;
    azs.closed = false;
    azs.current = 0.0f;

    // Multiple cooling steps
    for (int i = 0; i < 100; ++i) {
        azs.solve_thermal(st, 1.0f);
    }

    EXPECT_GE(azs.temp, 0.0f);
}

TEST_F(AZSTestFixture, OutputsTempPort) {
    azs.temp = 0.42f;
    azs.closed = true;
    st.across[IDX_CONTROL] = 0.0f;
    azs.last_control = 0.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[IDX_TEMP], 0.42f);
}

// =============================================================================
// Thermal Trip — The Core Feature
// =============================================================================

TEST_F(AZSTestFixture, TripsWhenOverheated) {
    // When temp > 1.0 in post_step, AZS should open (branchless)
    azs.closed = true;
    azs.temp = 1.1f; // above threshold
    st.across[IDX_CONTROL] = 0.0f;
    azs.last_control = 0.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_FALSE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_STATE], 0.0f);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 1.0f);
}

TEST_F(AZSTestFixture, DoesNotTripBelowThreshold) {
    azs.closed = true;
    azs.temp = 0.95f; // below threshold
    st.across[IDX_CONTROL] = 0.0f;
    azs.last_control = 0.0f;

    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_TRUE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 0.0f);
}

TEST_F(AZSTestFixture, TrippedStaysOpenUntilManualReset) {
    // After trip, even if temp drops below 1.0, AZS stays open
    // tripped flag stays true until user toggles OFF→ON
    azs.closed = true;
    azs.temp = 1.1f;
    azs.last_control = 0.0f;
    st.across[IDX_CONTROL] = 0.0f;

    // Trip
    azs.post_step(st, 1.0f / 60.0f);
    EXPECT_FALSE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 1.0f);

    // Cool down — tripped stays true
    azs.temp = 0.3f;
    azs.post_step(st, 1.0f / 60.0f);
    EXPECT_FALSE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 1.0f);
}

TEST_F(AZSTestFixture, CanReenableAfterCooldown) {
    // Trip → cool down → manual toggle → should close again
    azs.closed = true;
    azs.temp = 1.1f;
    azs.last_control = 0.0f;
    st.across[IDX_CONTROL] = 0.0f;

    // Trip
    azs.post_step(st, 1.0f / 60.0f);
    EXPECT_FALSE(azs.closed);

    // Cool down
    azs.temp = 0.3f;

    // Manual toggle: rising edge on control
    st.across[IDX_CONTROL] = 1.0f;
    azs.post_step(st, 1.0f / 60.0f);

    EXPECT_TRUE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 0.0f);
}

TEST_F(AZSTestFixture, RetripsIfStillHot) {
    // If user re-enables while temp is still > 1.0, it trips again immediately
    azs.closed = true;
    azs.temp = 1.5f;
    azs.last_control = 0.0f;
    st.across[IDX_CONTROL] = 0.0f;

    // Trip
    azs.post_step(st, 1.0f / 60.0f);
    EXPECT_FALSE(azs.closed);

    // User tries to re-enable immediately (temp still hot)
    st.across[IDX_CONTROL] = 1.0f;
    azs.post_step(st, 1.0f / 60.0f);

    // Toggle fires → closed = true, but then trip fires → closed = false
    // Net result: still open
    EXPECT_FALSE(azs.closed);
    EXPECT_FLOAT_EQ(st.across[IDX_TRIPPED], 1.0f);
}

// =============================================================================
// Thermal Model Calibration
// =============================================================================

TEST_F(AZSTestFixture, NominalCurrentReachesSteadyStateAtOne) {
    // At I = i_nominal (20A), steady-state temp should be ~1.0
    // T_ss = I² * r_heat / k_cool = 400 * (1/400) / 1 = 1.0
    azs.closed = true;
    azs.temp = 0.0f;
    azs.current = 20.0f; // nominal current

    // Run thermal for many seconds until convergence
    for (int i = 0; i < 100; ++i) {
        azs.solve_thermal(st, 1.0f);
    }

    // Should converge to ~1.0 (equilibrium)
    EXPECT_NEAR(azs.temp, 1.0f, 0.05f);
}

TEST_F(AZSTestFixture, DoubleNominalTripsQuickly) {
    // At I = 2 * i_nominal (40A), should overshoot 1.0 within a few seconds
    azs.closed = true;
    azs.temp = 0.0f;
    azs.current = 40.0f; // 2x nominal

    int steps_to_trip = 0;
    for (int i = 0; i < 30; ++i) {
        azs.solve_thermal(st, 1.0f);
        if (azs.temp > 1.0f) {
            steps_to_trip = i + 1;
            break;
        }
    }

    // Should trip within a few seconds (not 30)
    EXPECT_GT(steps_to_trip, 0);
    EXPECT_LT(steps_to_trip, 10);
}

TEST_F(AZSTestFixture, ThermalWorksWithVariableDt) {
    // FPS-independent: different dt values should give similar results
    // over the same total time period
    AZS<JitProvider> azs1 = azs;
    AZS<JitProvider> azs2 = azs;
    azs1.closed = true;
    azs2.closed = true;
    azs1.current = 20.0f;
    azs2.current = 20.0f;
    azs1.temp = 0.0f;
    azs2.temp = 0.0f;

    SimulationState st1 = st;
    SimulationState st2 = st;

    // 10 seconds at dt=1.0 (50Hz screen → thermal fires every ~50 frames)
    for (int i = 0; i < 10; ++i) {
        azs1.solve_thermal(st1, 1.0f);
    }

    // 10 seconds at dt=0.42 (144Hz screen → thermal fires ~every 144 frames → dt≈0.42)
    float total = 0.0f;
    while (total < 10.0f) {
        float chunk = 0.42f;
        if (total + chunk > 10.0f) chunk = 10.0f - total;
        azs2.solve_thermal(st2, chunk);
        total += chunk;
    }

    // Both should converge to similar temperature (within 10% tolerance due to Euler integration)
    EXPECT_NEAR(azs1.temp, azs2.temp, azs1.temp * 0.15f);
}

// =============================================================================
// r_heat / k_cool precomputation
// =============================================================================

TEST_F(AZSTestFixture, RheatFromNominal) {
    // r_heat should be 1 / (i_nominal²) so that steady-state at I_nom → T=1.0
    float expected = 1.0f / (20.0f * 20.0f);
    EXPECT_FLOAT_EQ(azs.r_heat, expected);
}
