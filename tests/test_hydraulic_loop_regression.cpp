/// Hydraulic loop regression tests: verify that ElectricPump, SolenoidValve,
/// and GidroAccumulator can form a closed hydraulic circuit with stable SOR
/// convergence.
///
/// Circuit topology:
///   [RefNode 28V] → ElectricPump.v_in
///   [Ground p=0]  → ElectricPump.p_in
///   ElectricPump.p_out → SolenoidValve.flow_in
///   SolenoidValve.flow_out → GidroAccumulator.p_in
///   GidroAccumulator.p_out → (output observation point)
///
/// This exercises the full hydraulic chain: pump generates pressure,
/// valve gates it, accumulator buffers and stores it.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/SOR_constants.h"
#include <cmath>

// =============================================================================
// Signal layout (shared across hydraulic loop tests)
// =============================================================================
//
// Signal indices:
//   0: v_bus       (electrical bus, fixed at 28V)
//   1: p_ground    (hydraulic ground, fixed at 0)
//   2: p_pump_out  (ElectricPump.p_out = SolenoidValve.flow_in)
//   3: p_valve_out (SolenoidValve.flow_out = GidroAccumulator.p_in)
//   4: p_accum_out (GidroAccumulator.p_out)
//   5: ctrl        (solenoid control voltage)

static constexpr size_t SIG_V_BUS       = 0;
static constexpr size_t SIG_P_GROUND    = 1;
static constexpr size_t SIG_P_PUMP_OUT  = 2;
static constexpr size_t SIG_P_VALVE_OUT = 3;
static constexpr size_t SIG_P_ACCUM_OUT = 4;
static constexpr size_t SIG_CTRL        = 5;
static constexpr size_t NUM_SIGNALS     = 6;

struct HydraulicLoop {
    ElectricPump<JitProvider> pump;
    SolenoidValve<JitProvider> valve;
    GidroAccumulator<JitProvider> accum;
    SimulationState st;

    HydraulicLoop(float max_p = 1000.0f, bool nc = true,
                  float precharge = 50.0f, float vol = 10.0f) {
        // Pump
        pump.max_pressure = max_p;
        pump.provider.indices[PortNames::v_in]  = SIG_V_BUS;
        pump.provider.indices[PortNames::p_in]  = SIG_P_GROUND;
        pump.provider.indices[PortNames::p_out] = SIG_P_PUMP_OUT;

        // Valve
        valve.normally_closed = nc;
        valve.provider.indices[PortNames::ctrl]     = SIG_CTRL;
        valve.provider.indices[PortNames::flow_in]  = SIG_P_PUMP_OUT;
        valve.provider.indices[PortNames::flow_out] = SIG_P_VALVE_OUT;

        // Accumulator
        accum.precharge_pressure = precharge;
        accum.volume = vol;
        accum.gas_volume = vol;
        accum.provider.indices[PortNames::p_in]  = SIG_P_VALVE_OUT;
        accum.provider.indices[PortNames::p_out] = SIG_P_ACCUM_OUT;
        accum.pre_load();

        // State
        st.across.resize(NUM_SIGNALS, 0.0f);
        st.through.resize(NUM_SIGNALS, 0.0f);
        st.conductance.resize(NUM_SIGNALS, 0.0f);
        st.inv_conductance.resize(NUM_SIGNALS, 0.0f);
    }

    void run_sor_steps(int steps, float v_bus, float ctrl, float dt = 1.0f / 5.0f) {
        const float omega = SOR::OMEGA;
        for (int i = 0; i < steps; ++i) {
            // Reset per-iteration accumulators
            for (size_t j = 0; j < NUM_SIGNALS; ++j) {
                st.through[j] = 0.0f;
                st.conductance[j] = 1e-6f;  // parasitic
            }

            // Fix v_bus
            float g_fix = 1e6f;
            st.conductance[SIG_V_BUS] += g_fix;
            st.through[SIG_V_BUS] += (v_bus - st.across[SIG_V_BUS]) * g_fix;

            // Fix p_ground at 0
            st.conductance[SIG_P_GROUND] += g_fix;
            st.through[SIG_P_GROUND] += (0.0f - st.across[SIG_P_GROUND]) * g_fix;

            // Fix control voltage
            st.conductance[SIG_CTRL] += g_fix;
            st.through[SIG_CTRL] += (ctrl - st.across[SIG_CTRL]) * g_fix;

            // Stamp all components
            pump.solve_hydraulic(st, dt);
            valve.solve_hydraulic(st, dt);
            accum.solve_hydraulic(st, dt);

            // Compute inv_conductance and SOR update
            for (size_t j = 0; j < NUM_SIGNALS; ++j) {
                st.inv_conductance[j] = 1.0f / st.conductance[j];
            }
            solve_sor_iteration(st.across.data(), st.through.data(),
                               st.inv_conductance.data(), NUM_SIGNALS, omega);
        }
    }

    void run_post_step(float dt = 1.0f / 5.0f) {
        accum.post_step(st, dt);
    }
};

// =============================================================================
// Test: Closed valve blocks pressure propagation
// =============================================================================

TEST(HydraulicLoopRegression, ClosedValve_BlocksPressure) {
    HydraulicLoop loop(1000.0f, true);  // normally_closed valve

    // Run with 28V supply, ctrl = 0V (valve closed because NC and ctrl < 12V)
    loop.run_sor_steps(200, 28.0f, 0.0f);

    // Pump output should have pressure (pump is running)
    EXPECT_GT(loop.st.across[SIG_P_PUMP_OUT], 100.0f)
        << "Pump output should have significant pressure with 28V input";

    // But downstream of the closed valve, pressure should be near zero
    // (only parasitic leakage through the valve's zero conductance)
    EXPECT_LT(loop.st.across[SIG_P_VALVE_OUT], 10.0f)
        << "Closed valve should block pressure from reaching downstream";

    EXPECT_LT(loop.st.across[SIG_P_ACCUM_OUT], 10.0f)
        << "Accumulator output should be near precharge since no flow enters";
}

// =============================================================================
// Test: Open valve passes pressure through to accumulator
// =============================================================================

TEST(HydraulicLoopRegression, OpenValve_PressurePropagates) {
    HydraulicLoop loop(1000.0f, true);  // normally_closed valve

    // Run with 28V supply, ctrl = 28V (valve opens because NC and ctrl > 12V)
    loop.run_sor_steps(300, 28.0f, 28.0f);

    // All hydraulic nodes should have significant pressure
    EXPECT_GT(loop.st.across[SIG_P_PUMP_OUT], 100.0f)
        << "Pump output should have pressure";

    EXPECT_GT(loop.st.across[SIG_P_VALVE_OUT], 50.0f)
        << "Open valve should allow pressure to pass through";

    EXPECT_GT(loop.st.across[SIG_P_ACCUM_OUT], 40.0f)
        << "Accumulator output should see pressure when valve is open";

    // All values should be finite (no SOR divergence)
    for (size_t i = 0; i < NUM_SIGNALS; ++i) {
        EXPECT_FALSE(std::isnan(loop.st.across[i]))
            << "Signal " << i << " is NaN - SOR diverged";
        EXPECT_FALSE(std::isinf(loop.st.across[i]))
            << "Signal " << i << " is Inf - SOR diverged";
    }
}

// =============================================================================
// Test: Accumulator buffers pressure after pump shutdown
// =============================================================================

TEST(HydraulicLoopRegression, Accumulator_BuffersPressure) {
    HydraulicLoop loop(1000.0f, true);

    // Phase 1: Charge the system (valve open, pump running)
    loop.run_sor_steps(300, 28.0f, 28.0f);
    loop.run_post_step();

    float p_accum_charged = loop.st.across[SIG_P_ACCUM_OUT];
    EXPECT_GT(p_accum_charged, 40.0f)
        << "Accumulator should be charged";

    // Phase 2: Shut off pump (v_bus = 0), close valve (ctrl = 0)
    // Accumulator should retain pressure from its gas spring
    loop.run_sor_steps(200, 0.0f, 0.0f);

    // The accumulator's gas spring should maintain some pressure
    float p_accum_after = loop.st.across[SIG_P_ACCUM_OUT];
    EXPECT_GT(p_accum_after, 10.0f)
        << "Accumulator should retain pressure from gas spring after pump shutdown";
}

// =============================================================================
// Test: No voltage = no pressure boost
// =============================================================================

TEST(HydraulicLoopRegression, NoPower_NoPressure) {
    HydraulicLoop loop(1000.0f, true);

    // Run with 0V supply, valve open
    loop.run_sor_steps(100, 0.0f, 28.0f);

    // target_p = 0 * 1000 / 28 = 0, so no pressure boost
    // All hydraulic nodes should be near the accumulator precharge or zero
    EXPECT_LT(std::abs(loop.st.across[SIG_P_PUMP_OUT]), 100.0f)
        << "No voltage should produce minimal pressure boost";
}

// =============================================================================
// Test: Determinism - same inputs produce same outputs
// =============================================================================

TEST(HydraulicLoopRegression, Determinism_SameInputsSameOutput) {
    auto run_once = [](float v_bus, float ctrl) -> float {
        HydraulicLoop loop(1000.0f, true);
        loop.run_sor_steps(200, v_bus, ctrl);
        return loop.st.across[SIG_P_ACCUM_OUT];
    };

    float p1 = run_once(28.0f, 28.0f);
    float p2 = run_once(28.0f, 28.0f);

    EXPECT_FLOAT_EQ(p1, p2)
        << "Same inputs must produce identical outputs (determinism)";
}

// =============================================================================
// Test: SOR stability - no divergence over many iterations
// =============================================================================

TEST(HydraulicLoopRegression, SOR_StableOverManyIterations) {
    HydraulicLoop loop(1000.0f, true);

    // Run a large number of SOR iterations with valve open
    loop.run_sor_steps(1000, 28.0f, 28.0f);

    for (size_t i = 0; i < NUM_SIGNALS; ++i) {
        EXPECT_FALSE(std::isnan(loop.st.across[i]))
            << "Signal " << i << " is NaN after 1000 SOR iterations";
        EXPECT_FALSE(std::isinf(loop.st.across[i]))
            << "Signal " << i << " is Inf after 1000 SOR iterations";
        EXPECT_LT(std::abs(loop.st.across[i]), 1e6f)
            << "Signal " << i << " diverged beyond reasonable bounds";
    }
}
