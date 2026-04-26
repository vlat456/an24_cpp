#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/state.h"
#include "core/solvers/jit/subsolvers/nodal_subsolver.h"
#include "jit_build_input_test_helper.h"
#include <cmath>

// ============================================================================
// Hydraulic Integration Tests — #301-E / #305 / #309
// Validates end-to-end hydraulic build + solve through the Simulator.
// Uses PressureRef as hydraulic boundary (FixedPressureNode, P=0).
//
// Circuit topology (all tests):
//   FuelTank.flow_out ── SolenoidValve.flow_in     (junction, unknown P)
//   SolenoidValve.flow_out ── PressureRef.p         (drain, P=0 fixed)
//   FuelTank.p_ref ── PressureRef.p                 (atmospheric reference)
//   Value(24V) ── SolenoidValve.ctrl               (opens normally_closed valve)
// ============================================================================

namespace {

// ---- Physical constants for hand-computed expected values ----
constexpr float GRAVITY = 9.81f;
constexpr float DENSITY = 0.78f;       // TS-1 kerosene (kg/L)
constexpr float TANK_HEIGHT = 1.0f;    // meters
constexpr float INTERNAL_R = 0.1f;    // kPa·s/L
constexpr float G_SRC = 1.0f / INTERNAL_R;  // = 10 L/(s·kPa)

// Full tank: level_frac = 1.0
constexpr float P_TH_FULL = DENSITY * GRAVITY * TANK_HEIGHT * 1.0f;

// ---- Shared hydraulic circuit topology ----

/// Hydraulic-only devices: FuelTank + SolenoidValve + Value(ctrl) + PressureRef(drain).
std::vector<DeviceInstance> hydraulic_devices() {
    return {
        make_device("tank", "FuelTank", {
            {"capacity", "1000.0"}, {"density", std::to_string(DENSITY)},
            {"level", "1000.0"}, {"consumption_rate", "0.0"},
            {"internal_r", std::to_string(INTERNAL_R)},
            {"tank_height", std::to_string(TANK_HEIGHT)}
        }),
        make_device("valve", "SolenoidValve", {
            {"normally_closed", "true"}, {"g_open", "10.0"}, {"g_closed", "0.0001"}
        }),
        make_device("ctrl_src", "Value", {{"value", "24.0"}}),
        make_device("drain", "PressureRef", {{"pressure", "0.0"}})
    };
}

/// Signal groups for the hydraulic-only circuit.
///   0: tank.flow_out ── valve.flow_in       (junction, unknown P)
///   1: valve.flow_out ── drain.p ── tank.p_ref  (drain + atmospheric ref)
///   2: ctrl_src.o ── valve.ctrl
///   3+: isolated component signals
std::vector<std::vector<std::string>> hydraulic_signal_groups() {
    return {
        {"tank.flow_out", "valve.flow_in"},
        {"valve.flow_out", "drain.p", "tank.p_ref"},
        {"ctrl_src.o", "valve.ctrl"},
        {"tank.level_out"},
        {"tank.p_source"},
        {"valve.state"}
    };
}

// ---- Shared combined (electrical + hydraulic) circuit topology ----

/// Combined electrical + hydraulic devices.
std::vector<DeviceInstance> combined_devices() {
    auto devs = std::vector<DeviceInstance>{
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("resistor", "Resistor", {{"conductance", "1.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };
    auto hydraulic = hydraulic_devices();
    devs.insert(devs.end(), hydraulic.begin(), hydraulic.end());
    return devs;
}

/// Signal groups for the combined circuit.
std::vector<std::vector<std::string>> combined_signal_groups() {
    auto groups = std::vector<std::vector<std::string>>{
        {"battery.v_out", "resistor.v_in"},
        {"resistor.v_out", "refnode.v", "battery.v_in"}
    };
    auto hydraulic = hydraulic_signal_groups();
    groups.insert(groups.end(), hydraulic.begin(), hydraulic.end());
    return groups;
}

/// Build a FuelTank → SolenoidValve → PressureRef circuit (build pipeline).
/// For Tests 1-3 that verify BuildResult directly.
std::pair<BuildResult, SimulationState> build_tank_valve_circuit() {
    auto input = make_jit_input(hydraulic_devices(), hydraulic_signal_groups());
    auto result = build_systems_dev(input);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        (void)st.allocate_signal(0.0f);
    }

    return {std::move(result), std::move(st)};
}

} // anonymous namespace

// ============================================================================
// Test 1: Build pipeline produces correct hydraulic plan with 3 elements
// ============================================================================

TEST(HydraulicIntegration, BuildProducesHydraulicIslands) {
    auto [result, st] = build_tank_valve_circuit();

    // All three hydraulic elements are connected → 1 island
    ASSERT_EQ(result.hydraulic.plan.islands.size(), 1u);

    const auto& island = result.hydraulic.plan.islands[0];

    // PressureSource + FlowBranch + FixedPressureNode
    ASSERT_EQ(island.elements.size(), 3u);

    int pressure_sources = 0;
    int flow_branches = 0;
    int fixed_nodes = 0;
    for (const auto& elem : island.elements) {
        switch (elem.kind) {
            case NodalElementKind::Source:  pressure_sources++; break;
            case NodalElementKind::Branch:      flow_branches++; break;
            case NodalElementKind::FixedNode: fixed_nodes++; break;
        }
    }
    EXPECT_EQ(pressure_sources, 1);
    EXPECT_EQ(flow_branches, 1);
    EXPECT_EQ(fixed_nodes, 1);
}

// ============================================================================
// Test 2: Handles assigned to solver-owned components
// ============================================================================

TEST(HydraulicIntegration, HandlesAssignedToComponents) {
    auto [result, st] = build_tank_valve_circuit();

    auto& tank_var = result.devices.at("tank");
    const auto* tank = std::get_if<FuelTank<JitProvider>>(&tank_var);
    ASSERT_NE(tank, nullptr);
    EXPECT_TRUE(is_valid(tank->hydraulic_handle));

    auto& valve_var = result.devices.at("valve");
    const auto* valve = std::get_if<SolenoidValve<JitProvider>>(&valve_var);
    ASSERT_NE(valve, nullptr);
    EXPECT_TRUE(is_valid(valve->hydraulic_handle));
}

// ============================================================================
// Test 3: Patch ops created for solver-owned components
// ============================================================================

TEST(HydraulicIntegration, PatchOpsCreated) {
    auto [result, st] = build_tank_valve_circuit();

    ASSERT_EQ(result.hydraulic.patch_ops.size(), 2u);

    int copy_signals = 0;
    int bool_switches = 0;
    for (const auto& op : result.hydraulic.patch_ops) {
        switch (op.kind) {
            case NodalPatchKind::CopySignal:  copy_signals++; break;
            case NodalPatchKind::BoolSwitch:  bool_switches++; break;
        }
    }
    EXPECT_EQ(copy_signals, 1);
    EXPECT_EQ(bool_switches, 1);
}

// ============================================================================
// Test 4: Pressure solve with hand-computed expected values (Simulator API)
// ============================================================================

TEST(HydraulicIntegration, SolvePressureDistribution) {
    // Use the Simulator pipeline for correct one-frame-delay convergence.
    // Manual bootstrap is error-prone — the Simulator handles execute→patch→solve→commit.
    Simulator<JIT_Solver> sim;
    auto input = make_jit_input(hydraulic_devices(), hydraulic_signal_groups());
    sim.start(input);

    // Run enough steps for one-frame-delay convergence
    for (int i = 0; i < 5; ++i) {
        sim.step(1.0 / 60.0);
    }

    // Expected: P_junction = G_SRC * P_TH_FULL / (G_SRC + g_open)
    // = 10 * 7.6518 / (10 + 10) = 3.8259 kPa
    const float g_open = 10.0f;
    const float expected_p_junction = G_SRC * P_TH_FULL / (G_SRC + g_open);

    auto junction_key = sim.resolve_signal_key("tank", "flow_out");
    EXPECT_NEAR(sim.get_signal_value(junction_key), expected_p_junction, 0.05f);

    // Drain should be 0 kPa (FixedPressureNode)
    auto drain_key = sim.resolve_signal_key("drain", "p");
    EXPECT_NEAR(sim.get_signal_value(drain_key), 0.0f, 0.01f);
}

// ============================================================================
// Test 5: Electrical + hydraulic coexist without interference (Simulator API)
// ============================================================================

TEST(HydraulicIntegration, ElectricalAndHydraulicCoexist) {
    auto input = make_jit_input(combined_devices(), combined_signal_groups());

    Simulator<JIT_Solver> sim;
    sim.start(input);

    // Run enough steps for one-frame-delay convergence
    for (int i = 0; i < 5; ++i) {
        sim.step(1.0 / 60.0);
    }

    // Electrical: V_junction = V_src * R_load / (R_src + R_load)
    const float expected_v_junction = 28.0f * 1.0f / (0.01f + 1.0f);
    auto v_key = sim.resolve_signal_key("battery", "v_out");
    EXPECT_NEAR(sim.get_signal_value(v_key), expected_v_junction, 0.1f);

    // Electrical ground should be 0V
    auto gnd_key = sim.resolve_signal_key("refnode", "v");
    EXPECT_NEAR(sim.get_signal_value(gnd_key), 0.0f, 0.01f);

    // Hydraulic: P_junction = G_SRC * P_TH_FULL / (G_SRC + g_open)
    const float expected_p_junction = G_SRC * P_TH_FULL / (G_SRC + 10.0f);
    auto junction_key = sim.resolve_signal_key("tank", "flow_out");
    EXPECT_NEAR(sim.get_signal_value(junction_key), expected_p_junction, 0.05f);

    // Drain should be 0 kPa
    auto drain_key = sim.resolve_signal_key("drain", "p");
    EXPECT_NEAR(sim.get_signal_value(drain_key), 0.0f, 0.01f);

    // Tank level should be 1.0 (full, no consumption)
    auto level_key = sim.resolve_signal_key("tank", "level_out");
    EXPECT_NEAR(sim.get_signal_value(level_key), 1.0f, 0.01f);
}

// ============================================================================
// Test 6: Simulator full pipeline — valve control + pressure solve
// ============================================================================

TEST(HydraulicIntegration, SimulatorStepWithHydraulic) {
    auto input = make_jit_input(combined_devices(), combined_signal_groups());

    Simulator<JIT_Solver> sim;
    sim.start(input);

    // Run enough steps for one-frame-delay convergence:
    //   Frame 0: commit writes valve.open=true (ctrl=24V > threshold)
    //   Frame 1: BoolSwitch patches g_open into element_value_a
    //   Frame 2+: steady state with correct pressure solve
    for (int i = 0; i < 5; ++i) {
        sim.step(1.0 / 60.0);
    }

    // Electrical verification
    const float expected_v = 28.0f * 1.0f / (0.01f + 1.0f);
    EXPECT_NEAR(sim.get_signal_value(sim.resolve_signal_key("battery", "v_out")),
                expected_v, 0.1f);

    // Hydraulic: p_source should show gravity pressure for full tank
    const float expected_p_source = DENSITY * GRAVITY * TANK_HEIGHT * 1.0f;
    auto p_key = sim.resolve_signal_key("tank", "p_source");
    EXPECT_NEAR(sim.get_signal_value(p_key), expected_p_source, 0.1f);

    // Valve state should be 1.0 (open: ctrl=24V)
    auto state_key = sim.resolve_signal_key("valve", "state");
    EXPECT_FLOAT_EQ(sim.get_signal_value(state_key), 1.0f);

    // Junction pressure: P = G_SRC * P_TH / (G_SRC + g_open) ≈ 3.826 kPa
    const float expected_p_junction = G_SRC * P_TH_FULL / (G_SRC + 10.0f);
    auto junction_key = sim.resolve_signal_key("tank", "flow_out");
    EXPECT_NEAR(sim.get_signal_value(junction_key), expected_p_junction, 0.05f);

    // Drain should be 0 kPa
    EXPECT_NEAR(sim.get_signal_value(sim.resolve_signal_key("drain", "p")), 0.0f, 0.01f);
}