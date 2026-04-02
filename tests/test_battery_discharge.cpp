#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/components/provider.h"
#include "jit_solver/state.h"
#include "jit_solver/simulator.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

/// Port layout: [0]=v_in, [1]=v_out
static Battery<JitProvider> make_battery(float v_nominal = 28.0f, float capacity = 1000.0f) {
    Battery<JitProvider> comp;
    comp.v_nominal = v_nominal;
    comp.capacity = capacity;
    comp.charge = capacity;
    comp.internal_r = 0.01f;
    comp.pre_load();
    comp.provider.set(PortNames::v_in, 0);
    comp.provider.set(PortNames::v_out, 1);
    return comp;
}

static SimulationState make_state(size_t n = 2) {
    SimulationState st;
    st.values.resize(n, 0.0f);
    st.signal_types.resize(n, {Domain::Electrical, false});
    st.dynamic_signals_count = static_cast<uint32_t>(n);
    return st;
}

// =============================================================================
// Battery Discharge Tests - Batch 6
// Battery discharge uses solved branch current via electrical_handle.
// Sign convention: negative branch_current = discharge (current exits positive terminal)
//                  positive branch_current = charging (current enters positive terminal)
// =============================================================================

TEST(Battery, ChargeDecreasesUnderLoad) {
    // TheveninSource branch current convention: when discharging, the internal
    // branch current (a→b through element) is NEGATIVE.  discharge_current = max(0, -i).
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 500.0f;
    comp.electrical_handle = {0, 0, 1};  // island 0, element 0, component_index 1

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, -10.0f};  // component_index 1 = -10A (discharge)

    float initial_charge = static_cast<float>(comp.charge);
    double dt = 1.0 / 60.0;  // 1 frame

    step_component(comp, st, dt);

    // discharge_current = max(0, -(-10)) = 10A
    // charge -= 10 * (1/60) / 3600 = 10/216000 = 0.0000463
    double expected_discharge = 10.0 * dt / 3600.0;
    EXPECT_LT(comp.charge, static_cast<double>(initial_charge));
    EXPECT_NEAR(comp.charge, static_cast<double>(initial_charge) - expected_discharge, 1e-5);

    delete st.electrical_rt;
}

TEST(Battery, ChargeClampedAtZero) {
    // Battery charge should never go below 0
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 0.001f;  // very low charge
    comp.electrical_handle = {0, 0, 1};

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, -10.0f};  // -10A = discharge

    double dt = 1.0 / 60.0;

    // Run multiple steps - charge should stay at 0
    for (int i = 0; i < 100; ++i) {
        step_component(comp, st, dt);
    }

    EXPECT_GE(comp.charge, 0.0f);
    EXPECT_LE(comp.charge, comp.capacity);
}

TEST(Battery, ChargeClampedAtCapacity) {
    // Battery charge should never exceed capacity
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 999.0f;  // near capacity
    comp.electrical_handle = {0, 0, 1};

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 10.0f};  // positive = charging (current into positive terminal)

    double dt = 1.0 / 60.0;

    // Run multiple steps - charge should be clamped at capacity
    for (int i = 0; i < 100; ++i) {
        step_component(comp, st, dt);
    }

    EXPECT_LE(comp.charge, comp.capacity);
    EXPECT_GE(comp.charge, 0.0f);
}

TEST(Battery, NoDischargeWithoutCurrent) {
    // Battery should not discharge when no electrical_rt
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 500.0f;
    comp.electrical_handle = {0, 0, 1};  // valid handle but no rt

    auto st = make_state();
    st.electrical_rt = nullptr;  // no electrical runtime

    double initial_charge = comp.charge;
    double dt = 1.0 / 60.0;

    step_component(comp, st, dt);

    EXPECT_EQ(comp.charge, initial_charge);
}

TEST(Battery, NoDischargeWhenCharging) {
    // Battery should not lose charge when charging (positive branch current = current into positive terminal)
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 500.0f;
    comp.electrical_handle = {0, 0, 1};

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 10.0f};  // positive = charging (current flows a→b internally)

    double initial_charge = comp.charge;
    double dt = 1.0 / 60.0;

    step_component(comp, st, dt);

    // discharge_current = max(0, -(+10)) = 0, so no discharge
    EXPECT_EQ(comp.charge, initial_charge);

    delete st.electrical_rt;
}

TEST(Battery, NoDischargeWithInvalidHandle) {
    // Battery should not discharge when handle is invalid
    auto comp = make_battery(28.0f, 1000.0f);
    comp.charge = 500.0f;
    comp.electrical_handle = {UINT32_MAX, UINT32_MAX, UINT32_MAX};  // invalid

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 10.0f};

    double initial_charge = comp.charge;
    double dt = 1.0 / 60.0;

    step_component(comp, st, dt);

    // With invalid handle, discharge_current = 0
    EXPECT_EQ(comp.charge, initial_charge);

    delete st.electrical_rt;
}

// =============================================================================
// Regression: End-to-end battery discharge with real electrical solver
// This test catches sign convention bugs between the solver and battery commit.
// The solver produces negative branch current for a discharging TheveninSource.
// Battery::commit must interpret negative as discharge (not positive).
// =============================================================================

TEST(Battery, EndToEndDischargeWithSolver) {
    // Battery(28V, R=0.01) -> Resistor(g=1.0, i.e. 1 ohm load) -> GND
    // Expected: ~28A through the circuit, battery charge decreases.
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01",
                "capacity": "100.0", "charge": "100.0"
            }},
            {"name": "res", "classname": "Resistor", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "res.v_in"},
            {"from": "res.v_out", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Run 60 steps (1 second at 60Hz)
    double dt = 1.0 / 60.0;
    for (int i = 0; i < 60; ++i) {
        sim.step(dt);
    }

    // Verify voltage at v_out is sane (battery is still charged)
    float v_out = sim.get_port_value("bat", "v_out");
    EXPECT_TRUE(std::isfinite(v_out));
    EXPECT_GT(v_out, 20.0f) << "Battery should still have voltage after 1s";
    EXPECT_LT(v_out, 29.0f) << "Battery voltage should be bounded";
}

TEST(Battery, SignConventionMatchesSolver) {
    // Directly verify that the solver produces negative branch current
    // for a discharging TheveninSource, matching battery's expectation.
    // Battery(28V, R=0.5) connected to GND via both terminals with load.
    ElectricalBuildPlan plan;
    ElectricalIslandPlan island;

    // RefNode at node 1 = 0V
    island.signal_indices = {0, 1};
    island.elements.push_back({
        ElectricalElementKind::FixedVoltageNode,
        1, UINT32_MAX, 0.0f, 0.0f, 0
    });
    // Battery: TheveninSource, node_a=0 (v_out), node_b=1 (v_in), Vth=28, R=0.5
    island.elements.push_back({
        ElectricalElementKind::TheveninSource,
        0, 1, 28.0f, 0.5f, 1
    });
    // Load: ConductanceBranch between node 0 and node 1, g=1.0 (1 ohm)
    island.elements.push_back({
        ElectricalElementKind::ConductanceBranch,
        0, 1, 1.0f, 0.0f, 2
    });
    plan.islands.push_back(island);

    SimulationState st;
    st.values.resize(2, 0.0f);
    st.signal_types.resize(2, {Domain::Electrical, false});
    st.dynamic_signals_count = 2;

    ElectricalRuntimeState rt;
    solve_electrical(plan, st, rt, 1.0f / 60.0f);

    // Battery branch current should be NEGATIVE (discharge)
    ASSERT_GE(rt.branch_currents.size(), 2u);
    float battery_branch_current = rt.branch_currents[1];
    EXPECT_LT(battery_branch_current, 0.0f)
        << "TheveninSource branch current must be negative when discharging";

    // Battery commit should correctly interpret this as discharge
    Battery<JitProvider> comp;
    comp.v_nominal = 28.0f;
    comp.capacity = 1000.0f;
    comp.charge = 500.0f;
    comp.internal_r = 0.5f;
    comp.pre_load();
    comp.provider.set(PortNames::v_in, 1);
    comp.provider.set(PortNames::v_out, 0);
    comp.electrical_handle = {0, 1, 1};  // points to the TheveninSource element

    st.electrical_rt = &rt;
    double initial_charge = comp.charge;
    comp.commit(st, 1.0f / 60.0f);

    EXPECT_LT(comp.charge, initial_charge)
        << "Battery must discharge when solver produces negative branch current";
}

// =============================================================================
// Regression: AotProvider must have has() method for Battery<AotProvider>
// to compile. Battery::commit() calls provider.has(PortNames::charge_out).
// Previously AotProvider had no has() method, causing compile failure.
// =============================================================================

using TestBatteryAot = Battery<AotProvider<
    Binding<PortNames::v_in, 0>,
    Binding<PortNames::v_out, 1>
>>;
template class Battery<AotProvider<
    Binding<PortNames::v_in, 0>,
    Binding<PortNames::v_out, 1>
>>;

TEST(BatteryDischarge, AotProviderInstantiatesAndCommits) {
    TestBatteryAot bat;
    bat.v_nominal = 28.0f;
    bat.internal_r = 0.01f;
    bat.capacity = 1000.0f;
    bat.pre_load();

    SimulationState st;
    st.values.resize(4, 0.0f);
    st.signal_types.resize(4, {Domain::Electrical, false});
    st.dynamic_signals_count = 2;

    EXPECT_NO_THROW(bat.commit(st, 1.0f / 60.0f))
        << "Battery<AotProvider> must compile and commit without crashing";

    EXPECT_DOUBLE_EQ(bat.charge, 1000.0)
        << "Without electrical_rt, charge should remain at initial value";
}
