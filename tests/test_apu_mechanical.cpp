#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static RU19A<JitProvider> make_apu() {
    RU19A<JitProvider> apu;
    apu.target_rpm = 16000.0f;
    apu.current_rpm = 0.0f;
    apu.spinup_inertia = 1.0f;
    apu.spindown_inertia = 0.02f;
    apu.crank_time = 2.0f;
    apu.ignition_time = 3.0f;
    apu.runup_time = 8.0f;
    apu.start_timeout = 30.0f;
    apu.t4 = 20.0f;
    apu.t4_target = 400.0f;
    apu.t4_max = 750.0f;
    apu.ambient_temp = 20.0f;
    apu.auto_start = false;  // manual control for tests

    // Port indices
    apu.provider.indices[PortNames::v_start] = 0;
    apu.provider.indices[PortNames::v_bus] = 1;
    apu.provider.indices[PortNames::rpm_out] = 2;
    apu.provider.indices[PortNames::t4_out] = 3;
    apu.provider.indices[PortNames::k_mod] = 4;
    return apu;
}

static SimulationState make_state(size_t n = 8) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    st.inv_conductance.resize(n, 0.0f);
    st.signal_types.resize(n, {Domain::Electrical, false});
    st.dynamic_signals_count = static_cast<uint32_t>(n);
    return st;
}

// =============================================================================
// State Machine Tests
// =============================================================================

TEST(APUMechanicalTest, InitialState_IsOFF) {
    auto apu = make_apu();
    EXPECT_EQ(apu.state, APUState::OFF);
    EXPECT_FLOAT_EQ(apu.current_rpm, 0.0f);
}

TEST(APUMechanicalTest, ManualStart_TransitionsToCranking) {
    auto apu = make_apu();
    apu.start();
    EXPECT_EQ(apu.state, APUState::CRANKING);
}

TEST(APUMechanicalTest, StartFromNonOFF_NoEffect) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.start();
    // start() should only work from OFF
    EXPECT_EQ(apu.state, APUState::RUNNING);
}

TEST(APUMechanicalTest, Stop_TransitionsToStopping) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.stop();
    EXPECT_EQ(apu.state, APUState::STOPPING);
}

TEST(APUMechanicalTest, IsStarterActive_TrueInCrankingAndIgnition) {
    auto apu = make_apu();

    apu.state = APUState::CRANKING;
    EXPECT_TRUE(apu.is_starter_active());

    apu.state = APUState::IGNITION;
    EXPECT_TRUE(apu.is_starter_active());

    apu.state = APUState::RUNNING;
    EXPECT_FALSE(apu.is_starter_active());

    apu.state = APUState::OFF;
    EXPECT_FALSE(apu.is_starter_active());
}

// =============================================================================
// Auto-start via v_start threshold
// =============================================================================

TEST(APUMechanicalTest, AutoStart_TriggeredByVoltage) {
    auto apu = make_apu();
    apu.auto_start = true;
    auto st = make_state();
    st.across[0] = 24.0f;  // v_start = 24V (above 10V threshold)

    apu.finalize_step(st, 1.0f / 60.0f);

    EXPECT_EQ(apu.state, APUState::CRANKING);
}

TEST(APUMechanicalTest, AutoStart_NotTriggeredBelowThreshold) {
    auto apu = make_apu();
    apu.auto_start = true;
    auto st = make_state();
    st.across[0] = 5.0f;  // v_start = 5V (below 10V threshold)

    apu.finalize_step(st, 1.0f / 60.0f);

    EXPECT_EQ(apu.state, APUState::OFF);
}

// =============================================================================
// Cranking → Ignition transition
// =============================================================================

TEST(APUMechanicalTest, Cranking_TransitionsToIgnition_AfterCrankTime) {
    auto apu = make_apu();
    apu.state = APUState::CRANKING;
    auto st = make_state();
    st.across[1] = 24.0f;  // bus voltage for voltage_factor

    float dt = 1.0f / 60.0f;

    // Run for less than crank_time (2s)
    for (int i = 0; i < 100; ++i) {  // 100 * 1/60 = 1.67s
        apu.solve_mechanical(st, dt);
        apu.finalize_step(st, dt);
    }
    EXPECT_EQ(apu.state, APUState::CRANKING);

    // Run until past crank_time
    for (int i = 0; i < 30; ++i) {  // +0.5s = 2.17s total
        apu.solve_mechanical(st, dt);
        apu.finalize_step(st, dt);
    }
    EXPECT_EQ(apu.state, APUState::IGNITION);
}

// =============================================================================
// Ignition → Running transition
// =============================================================================

TEST(APUMechanicalTest, Ignition_TransitionsToRunning_AfterIgnitionTime) {
    auto apu = make_apu();
    apu.state = APUState::IGNITION;
    apu.timer = 0.0f;
    auto st = make_state();

    float dt = 1.0f / 60.0f;

    // Run for less than ignition_time (3s)
    for (int i = 0; i < 150; ++i) {  // 150 * 1/60 = 2.5s
        apu.solve_mechanical(st, dt);
        apu.finalize_step(st, dt);
    }
    EXPECT_EQ(apu.state, APUState::IGNITION);

    // Run until past ignition_time
    for (int i = 0; i < 60; ++i) {  // +1.0s = 3.5s total
        apu.solve_mechanical(st, dt);
        apu.finalize_step(st, dt);
    }
    EXPECT_EQ(apu.state, APUState::RUNNING);
}

// =============================================================================
// RPM behavior during cranking
// =============================================================================

TEST(APUMechanicalTest, Cranking_RPMIncreases) {
    auto apu = make_apu();
    apu.state = APUState::CRANKING;
    auto st = make_state();
    st.across[1] = 24.0f;  // bus voltage

    float dt = 1.0f / 60.0f;
    float initial_rpm = apu.current_rpm;

    for (int i = 0; i < 60; ++i) {  // 1 second
        apu.solve_mechanical(st, dt);
    }

    EXPECT_GT(apu.current_rpm, initial_rpm);
    // During cranking, target is 2000 RPM * voltage_factor
    // RPM should be positive and increasing but not beyond target
    EXPECT_GT(apu.current_rpm, 0.0f);
    EXPECT_LE(apu.current_rpm, 2000.0f);
}

TEST(APUMechanicalTest, Cranking_RPMScalesWithVoltage) {
    // Lower bus voltage should result in slower cranking
    auto apu_high = make_apu();
    apu_high.state = APUState::CRANKING;
    auto st_high = make_state();
    st_high.across[1] = 24.0f;

    auto apu_low = make_apu();
    apu_low.state = APUState::CRANKING;
    auto st_low = make_state();
    st_low.across[1] = 14.0f;  // low battery

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        apu_high.solve_mechanical(st_high, dt);
        apu_low.solve_mechanical(st_low, dt);
    }

    // Higher voltage → higher RPM
    EXPECT_GT(apu_high.current_rpm, apu_low.current_rpm);
}

// =============================================================================
// Stopping behavior
// =============================================================================

TEST(APUMechanicalTest, Stopping_RPMDecreases) {
    auto apu = make_apu();
    apu.state = APUState::STOPPING;
    apu.current_rpm = 8000.0f;
    auto st = make_state();

    float dt = 1.0f / 60.0f;
    float initial_rpm = apu.current_rpm;

    for (int i = 0; i < 60; ++i) {
        apu.solve_mechanical(st, dt);
    }

    EXPECT_LT(apu.current_rpm, initial_rpm);
    EXPECT_GE(apu.current_rpm, 0.0f);
}

TEST(APUMechanicalTest, Stopping_TransitionsToOFF_WhenRPMZero) {
    auto apu = make_apu();
    apu.state = APUState::STOPPING;
    apu.current_rpm = 10.0f;  // Very low RPM
    auto st = make_state();

    float dt = 1.0f / 60.0f;

    // Exponential decay with spindown_inertia=0.02 and factor=2.0 has
    // a time constant of ~25s. From 10 RPM to 0.1 RPM threshold takes
    // ~115s (~6900 steps). Allow enough budget for full spindown.
    for (int i = 0; i < 8000; ++i) {
        apu.solve_mechanical(st, dt);
        apu.finalize_step(st, dt);
        if (apu.state == APUState::OFF) break;
    }

    EXPECT_EQ(apu.state, APUState::OFF);
    EXPECT_FLOAT_EQ(apu.current_rpm, 0.0f);
}

// =============================================================================
// RPM output signal (percentage 0-100)
// =============================================================================

TEST(APUMechanicalTest, RPMOut_IsPercentage) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.current_rpm = 8000.0f;  // 50% of 16000
    auto st = make_state();

    apu.finalize_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 50.0f, 0.1f);  // rpm_out = 50%
}

TEST(APUMechanicalTest, RPMOut_AtFullSpeed) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.current_rpm = 16000.0f;  // 100% of target
    auto st = make_state();

    apu.finalize_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 100.0f, 0.1f);  // rpm_out = 100%
}

// =============================================================================
// RPM clamping
// =============================================================================

TEST(APUMechanicalTest, RPM_NeverNegative) {
    auto apu = make_apu();
    apu.state = APUState::OFF;
    apu.current_rpm = 0.0f;
    auto st = make_state();

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        apu.solve_mechanical(st, dt);
    }

    EXPECT_GE(apu.current_rpm, 0.0f);
}

TEST(APUMechanicalTest, RPM_NeverExceedsTarget) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.current_rpm = 15900.0f;  // near target
    auto st = make_state();

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; ++i) {
        apu.solve_mechanical(st, dt);
    }

    EXPECT_LE(apu.current_rpm, apu.target_rpm);
}

// =============================================================================
// Electrical stamping in different states
// =============================================================================

TEST(APUMechanicalTest, OFF_NoElectricalStamping) {
    auto apu = make_apu();
    apu.state = APUState::OFF;
    auto st = make_state();
    st.across[0] = 24.0f;  // v_start
    st.across[1] = 28.0f;  // v_bus

    apu.solve_electrical(st, 1.0f / 60.0f);

    // OFF state should not stamp anything
    EXPECT_FLOAT_EQ(st.through[0], 0.0f);
    EXPECT_FLOAT_EQ(st.through[1], 0.0f);
    EXPECT_FLOAT_EQ(st.conductance[0], 0.0f);
    EXPECT_FLOAT_EQ(st.conductance[1], 0.0f);
}

TEST(APUMechanicalTest, Cranking_DrawsStarterCurrent) {
    auto apu = make_apu();
    apu.state = APUState::CRANKING;
    apu.current_rpm = 0.0f;
    auto st = make_state();
    st.across[0] = 24.0f;  // v_start

    apu.solve_electrical(st, 1.0f / 60.0f);

    // Starter should draw current from v_start (through < 0, conductance > 0)
    EXPECT_GT(st.conductance[0], 0.0f);
    // Net through should be negative (drain) when back-EMF is zero
    EXPECT_LT(st.through[0], 0.0f);
}

TEST(APUMechanicalTest, Running_GeneratesCurrent) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.current_rpm = 12000.0f;  // 75% - above generation threshold
    auto st = make_state();
    st.across[1] = 28.0f;  // v_bus
    st.across[4] = 1.0f;   // k_mod = 1.0

    apu.solve_electrical(st, 1.0f / 60.0f);

    // Generator should stamp conductance on v_bus
    EXPECT_GT(st.conductance[1], 0.0f);
}

// =============================================================================
// Thermal protection
// =============================================================================

TEST(APUMechanicalTest, ThermalOvertemp_ForcesStop) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.t4 = 740.0f;  // Near max (750°C)
    apu.t4_target = 800.0f;  // Force overtemp
    auto st = make_state();

    float dt = 1.0f;  // Large dt to push temp quickly

    for (int i = 0; i < 100; ++i) {
        apu.solve_thermal(st, dt);
        if (apu.state == APUState::STOPPING) break;
    }

    EXPECT_EQ(apu.state, APUState::STOPPING);
}

TEST(APUMechanicalTest, T4Output_ReflectsInternalTemp) {
    auto apu = make_apu();
    apu.state = APUState::RUNNING;
    apu.t4 = 350.0f;
    auto st = make_state();

    apu.solve_thermal(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[3], apu.t4);
}

// =============================================================================
// Full startup sequence (integration-style)
// =============================================================================

TEST(APUMechanicalTest, FullStartupSequence) {
    auto apu = make_apu();
    auto st = make_state();
    st.across[0] = 24.0f;  // v_start
    st.across[1] = 24.0f;  // v_bus
    st.across[4] = 1.0f;   // k_mod

    float dt = 1.0f / 60.0f;

    // Start
    apu.start();
    EXPECT_EQ(apu.state, APUState::CRANKING);

    // Run through entire startup sequence
    bool reached_running = false;
    for (int i = 0; i < 600; ++i) {  // 10 seconds max
        st.through.assign(st.through.size(), 0.0f);
        st.conductance.assign(st.conductance.size(), 0.0f);

        apu.solve_electrical(st, dt);
        apu.solve_mechanical(st, dt);
        apu.solve_thermal(st, dt);
        apu.finalize_step(st, dt);

        if (apu.state == APUState::RUNNING) {
            reached_running = true;
            break;
        }
    }

    EXPECT_TRUE(reached_running) << "APU did not reach RUNNING state within 10 seconds";
    EXPECT_GT(apu.current_rpm, 0.0f);
    EXPECT_GT(apu.t4, apu.ambient_temp);
}
