/// Regression tests for port mapping and logical gate integration
/// Catches the bug where ports like "A", "B", "Vin" were missing from
/// string_to_port_name, causing all logical gates to silently read signal[0].

#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/SOR_constants.h"
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"


// =============================================================================
// Regression: string_to_port_name covers every port in every component
// =============================================================================

TEST(PortMapRegression, AllComponentPortsAreInStringToPortName) {
    // get_component_ports returns codegen'd port lists for every component
    // string_to_port_name must resolve every one of them
    std::vector<std::string> classnames = {
        "AND", "OR", "XOR", "NOT", "NAND",
        "Any_V_to_Bool", "Positive_V_to_Bool",
        "Subtract", "Comparator", "Merger", "Splitter",
        "Battery", "Bus", "RefNode", "Switch", "Relay",
        "Resistor", "Load", "IndicatorLight", "Voltmeter",
        "HoldButton", "DMR400", "RU19A", "RUG82", "GS24",
        "Generator", "Inverter", "Transformer",
        "LerpNode", "InertiaNode", "AGK47", "Gyroscope",
        "ElectricHeater", "ElectricPump", "Radiator",
        "SolenoidValve", "TempSensor", "HighPowerLoad",
        "BlueprintInput", "BlueprintOutput",
        "P", "PI", "PD", "PID"
    };

    for (const auto& cls : classnames) {
        auto ports = get_component_ports(cls);
        ASSERT_FALSE(ports.empty()) << "No ports for component: " << cls;
        for (const auto& port : ports) {
            auto result = string_to_port_name(port);
            EXPECT_TRUE(result.has_value())
                << "Port '" << port << "' of component '" << cls
                << "' is NOT in string_to_port_name! Re-run codegen.";
        }
    }
}

// =============================================================================
// Regression: Logical gate reads correct wired signals (not default index 0)
// =============================================================================

static void run_step(BuildResult& result, SimulationState& state, float dt) {
    // Phase 1: passive electrical stamp
    state.clear_through();
    for (auto* v : result.phase_components.electrical_passive) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.stamp_electrical_passive(state, dt); })
                c.stamp_electrical_passive(state, dt);
            else if constexpr (requires { c.solve_electrical(state, dt); })
                c.solve_electrical(state, dt);
        }, *v);
    }

    // Phase 2: first SOR pass
    state.precompute_inv_conductance();
    solve_sor_iteration(state.across.data(), state.through.data(),
        state.inv_conductance.data(), state.dynamic_signals_count, SOR::OMEGA);

    // Phase 3: observers
    for (auto* v : result.phase_components.electrical_observer) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.observe_electrical(state, dt); })
                c.observe_electrical(state, dt);
        }, *v);
    }

    // Phase 4: logical solve pass 1 (feeds actuator cmd inputs)
    for (auto* v : result.phase_components.logical) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.solve_logical(state, dt); })
                c.solve_logical(state, dt);
        }, *v);
    }

    // Phase 5: control commit
    for (auto* v : result.phase_components.control_commit) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.commit_control(state, dt); })
                c.commit_control(state, dt);
        }, *v);
    }

    // Phase 5: actuator electrical stamp + second SOR
    state.clear_through();
    for (auto* v : result.phase_components.electrical_passive) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.stamp_electrical_passive(state, dt); })
                c.stamp_electrical_passive(state, dt);
            else if constexpr (requires { c.solve_electrical(state, dt); })
                c.solve_electrical(state, dt);
        }, *v);
    }
    for (auto* v : result.phase_components.electrical_actuator) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.stamp_electrical_actuator(state, dt); })
                c.stamp_electrical_actuator(state, dt);
        }, *v);
    }
    state.precompute_inv_conductance();
    solve_sor_iteration(state.across.data(), state.through.data(),
        state.inv_conductance.data(), state.dynamic_signals_count, SOR::OMEGA);

    // Phase 7: logical solve pass 2 (reads converged actuator outputs)
    for (auto* v : result.phase_components.logical) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.solve_logical(state, dt); })
                c.solve_logical(state, dt);
        }, *v);
    }

    // Phase 9: finalize
    for (auto& [name, v] : result.devices) {
        std::visit([&](auto& c) {
            if constexpr (requires { c.finalize_step(state, dt); })
                c.finalize_step(state, dt);
        }, v);
    }
}

TEST(PortMapRegression, AND_Gate_Reads_Correct_Signals) {
    // Battery(28V) -> V_to_Bool -> AND.A (should be 1)
    // HoldButton (not pressed) -> AND.B (should be 0)
    // AND.o should be 0 (not 1!)
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "inv_internal_r": "100.0",
                "capacity": "1000.0", "inv_capacity": "0.001", "charge": "1000.0"
            }},
            {"name": "bus", "classname": "Bus"},
            {"name": "v2b", "classname": "Positive_V_to_Bool"},
            {"name": "hb", "classname": "HoldButton", "params": {"idle": "0.0"}},
            {"name": "and_1", "classname": "AND"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "bus.v"},
            {"from": "bus.v", "to": "v2b.Vin"},
            {"from": "bus.v", "to": "hb.v_in"},
            {"from": "v2b.o", "to": "and_1.A"},
            {"from": "hb.state", "to": "and_1.B"}
        ]
    })";

    ParserContext ctx = parse_json(std::string(json));
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections)
        connections.push_back({c.from, c.to});

    BuildResult result = build_systems_dev(ctx.devices, connections);
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Init ground
    auto gnd_it = result.port_to_signal.find("gnd.v");
    if (gnd_it != result.port_to_signal.end())
        state.across[gnd_it->second] = 0.0f;

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 20; ++i)
        run_step(result, state, dt);

    auto get = [&](const std::string& port) {
        return state.across[result.port_to_signal.at(port)];
    };

    // Wired signals must share indices
    EXPECT_EQ(result.port_to_signal.at("v2b.Vin"), result.port_to_signal.at("bus.v"))
        << "v2b.Vin must be wired to bus.v";
    EXPECT_EQ(result.port_to_signal.at("and_1.A"), result.port_to_signal.at("v2b.o"))
        << "and_1.A must be wired to v2b.o";
    EXPECT_EQ(result.port_to_signal.at("and_1.B"), result.port_to_signal.at("hb.state"))
        << "and_1.B must be wired to hb.state";

    // Bus should have ~28V
    EXPECT_GT(get("bus.v"), 20.0f);

    // V_to_Bool(28V) -> 1.0
    EXPECT_NEAR(get("v2b.o"), 1.0f, 0.01f)
        << "Positive_V_to_Bool must output 1.0 when input > 0V";

    // HoldButton not pressed -> state = 0.0
    EXPECT_NEAR(get("hb.state"), 0.0f, 0.01f);

    // AND: A=1, B=0 -> output must be 0
    EXPECT_NEAR(get("and_1.o"), 0.0f, 0.01f)
        << "AND(1,0) must output 0, not 1! (port mapping regression)";
}

TEST(PortMapRegression, NOT_Gate_Reads_Correct_Input) {
    // NOT gate with input wired to a 1.0 source -> output should be 0
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "inv_internal_r": "100.0",
                "capacity": "1000.0", "inv_capacity": "0.001", "charge": "1000.0"
            }},
            {"name": "v2b", "classname": "Positive_V_to_Bool"},
            {"name": "not_1", "classname": "NOT"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "v2b.Vin"},
            {"from": "v2b.o", "to": "not_1.A"}
        ]
    })";

    ParserContext ctx = parse_json(std::string(json));
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections)
        connections.push_back({c.from, c.to});

    BuildResult result = build_systems_dev(ctx.devices, connections);
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    auto gnd_it = result.port_to_signal.find("gnd.v");
    if (gnd_it != result.port_to_signal.end())
        state.across[gnd_it->second] = 0.0f;

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 20; ++i)
        run_step(result, state, dt);

    auto get = [&](const std::string& port) {
        return state.across[result.port_to_signal.at(port)];
    };

    // V_to_Bool reads 28V -> outputs 1.0
    EXPECT_NEAR(get("v2b.o"), 1.0f, 0.01f);
    // NOT(1) -> 0
    EXPECT_NEAR(get("not_1.o"), 0.0f, 0.01f)
        << "NOT(1) must output 0! (port mapping regression)";
}

TEST(PortMapRegression, Subtract_Reads_Both_Inputs) {
    // Subtract: A=28V, B=0V -> o should be ~28
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "inv_internal_r": "100.0",
                "capacity": "1000.0", "inv_capacity": "0.001", "charge": "1000.0"
            }},
            {"name": "sub", "classname": "Subtract"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sub.A"},
            {"from": "gnd.v", "to": "sub.B"}
        ]
    })";

    ParserContext ctx = parse_json(std::string(json));
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections)
        connections.push_back({c.from, c.to});

    BuildResult result = build_systems_dev(ctx.devices, connections);
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    auto gnd_it = result.port_to_signal.find("gnd.v");
    if (gnd_it != result.port_to_signal.end())
        state.across[gnd_it->second] = 0.0f;

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 20; ++i)
        run_step(result, state, dt);

    auto get = [&](const std::string& port) {
        return state.across[result.port_to_signal.at(port)];
    };

    // A=~28V, B=0V -> o = 28
    EXPECT_GT(get("sub.o"), 20.0f)
        << "Subtract(28, 0) must output ~28! (port A/B mapping regression)";
}

TEST(PortMapRegression, Subtract_GSC_Topology_SignalIndices) {
    // Reproduce the GSC blueprint topology around subtract_1:
    // refnode_3 (28.5V) -> subtract_1:A
    // splitter_1:o2     -> subtract_1:B  (splitter aliases o2->i)
    // subtract_1:o      -> (disconnected or voltmeter)
    // ControlledVoltageSource v_pos -> splitter_1:i
    // GND RefNode -> CVS v_neg
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "target", "classname": "RefNode", "params": {"value": "28.5"}},
            {"name": "cvs", "classname": "ControlledVoltageSource", "params": {
                "gain": "1.0", "offset": "0.0", "min_v": "0.0", "max_v": "35.0",
                "r_internal": "0.05"
            }},
            {"name": "splitter", "classname": "Splitter"},
            {"name": "sub", "classname": "Subtract"},
            {"name": "resistor", "classname": "Resistor", "params": {"conductance": "0.1"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "cvs.v_neg"},
            {"from": "cvs.v_pos", "to": "splitter.i"},
            {"from": "splitter.o2", "to": "sub.B"},
            {"from": "target.v", "to": "sub.A"},
            {"from": "splitter.o1", "to": "resistor.v_in"},
            {"from": "resistor.v_out", "to": "gnd.v"}
        ]
    })";

    ParserContext ctx = parse_json(std::string(json));
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections)
        connections.push_back({c.from, c.to});

    BuildResult result = build_systems_dev(ctx.devices, connections);

    // Verify signal indices are DISTINCT for A, B, and o
    uint32_t sig_A = result.port_to_signal.at("sub.A");
    uint32_t sig_B = result.port_to_signal.at("sub.B");
    uint32_t sig_o = result.port_to_signal.at("sub.o");

    // A and B must be different signals
    EXPECT_NE(sig_A, sig_B)
        << "sub.A and sub.B share the same signal index " << sig_A
        << " — wiring bug!";

    // o must be different from A
    EXPECT_NE(sig_o, sig_A)
        << "sub.o and sub.A share signal index " << sig_A
        << " — output is fused with input A!";

    // o must be different from B
    EXPECT_NE(sig_o, sig_B)
        << "sub.o and sub.B share signal index " << sig_B
        << " — output is fused with input B!";

    // B should be the same signal as cvs.v_pos (via splitter alias)
    uint32_t sig_cvs_vpos = result.port_to_signal.at("cvs.v_pos");
    EXPECT_EQ(sig_B, sig_cvs_vpos)
        << "sub.B should share signal with cvs.v_pos through splitter alias";

    // A should be the same signal as target.v
    uint32_t sig_target = result.port_to_signal.at("target.v");
    EXPECT_EQ(sig_A, sig_target)
        << "sub.A should share signal with target.v";

    // Now actually simulate and check Subtract output
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    // Set RefNode values
    state.across[result.port_to_signal.at("gnd.v")] = 0.0f;
    state.across[result.port_to_signal.at("target.v")] = 28.5f;

    // Give cvs a cmd signal — set it to 28.0V
    auto cmd_it = result.port_to_signal.find("cvs.cmd");
    if (cmd_it != result.port_to_signal.end()) {
        state.across[cmd_it->second] = 28.0f;
    }

    state.resize_buffers(result.signal_count);

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
        run_step(result, state, dt);

    float sub_A = state.across[sig_A];
    float sub_B = state.across[sig_B];
    float sub_o = state.across[sig_o];

    // A should be ~28.5 (fixed RefNode)
    EXPECT_NEAR(sub_A, 28.5f, 0.1f) << "sub.A should read TargetV RefNode";

    // B should be ~28.0 (CVS output through splitter)
    EXPECT_GT(sub_B, 10.0f) << "sub.B should read CVS v_pos (~28V), got " << sub_B;

    // o should be A - B ≈ 0.5
    EXPECT_NEAR(sub_o, sub_A - sub_B, 0.5f)
        << "sub.o should be A-B=" << (sub_A - sub_B) << ", got " << sub_o;
}
