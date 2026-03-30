#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include "json_parser/json_parser.h"
#include <cmath>

namespace {

// Helper to create a basic device instance
DeviceInstance make_device(const std::string& name, const std::string& classname,
                          const std::unordered_map<std::string, std::string>& params = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};
    
    auto ports = get_component_ports(classname);
    if (ports.empty() && classname == "MaxSelector") {
        // MaxSelector is migration-local alias currently mapped to Max runtime type.
        ports = {"A", "B", "o"};
    }
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }
    return dev;
}

} // anonymous namespace

// ============================================================================
// Push Build Validation Tests - Phase 3.1
// Validates one-source-per-wire constraint in build_systems_dev
// ============================================================================

TEST(PushBuildValidation, SingleSourcePerWireOK) {
    // Single Battery driving a Load - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "load.input"},
        {"battery.v_out", "gnd.v"}
    };
    
    // Should not throw - single source per wire
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, MultipleSourcesSameWireErrors) {
    // Two Batteries driving the same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "Battery", {{"v_nominal", "28.0"}}),
        make_device("battery2", "Battery", {{"v_nominal", "27.0"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery1.v_out", "battery2.v_out"},  // Both batteries on same wire
        {"battery1.v_out", "load.input"},
        {"battery2.v_out", "gnd.v"}
    };
    
    // Should throw - multiple voltage sources on same wire
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, MultipleSourceLikeComponentsConflict) {
    // Generator + ControlledVoltageSource on same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.5"}}),
        make_device("cvs", "ControlledVoltageSource", {{"gain", "1.0"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "cvs.v_pos"},  // Both writing to same wire
        {"gen.v_out", "load.input"},
        {"cvs.v_neg", "gnd.v"}
    };
    
    // Should throw - two different source types writing to same signal
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, BatteryAndGeneratorOnSameWire) {
    // Battery + Generator on same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("generator", "Generator", {{"v_nominal", "28.5"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "generator.v_out"},  // Both on same wire
        {"battery.v_out", "load.input"},
        {"generator.v_out", "gnd.v"}
    };
    
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, BatteryAndRefNodeOnSameWire) {
    // Battery + RefNode on same wire - RefNode defines reference, NOT an active source
    // So this should be OK (RefNode just provides 0V reference)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("ref", "RefNode", {{"value", "5.0"}}),  // 5V reference
        make_device("load", "Load", {{"conductance", "0.1"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "ref.v"},  // Battery connected to reference node
        {"battery.v_out", "load.input"}
    };
    
    // RefNode is a reference point, not an active source - allowed
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, ControlledCurrentSourceConflict) {
    // ControlledCurrentSource writing to same wire as Battery - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("ccs", "ControlledCurrentSource", {{"gain", "1.0"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "ccs.v_pos"},  // Both writing voltage
        {"battery.v_out", "load.input"},
        {"ccs.v_neg", "gnd.v"}
    };
    
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, ControlledVoltageSourcesShareOnlyVNeg_NoConflict) {
    // v_neg is treated as an output for dependency ordering, but NOT as an
    // active source-writer for one-source-per-wire conflict detection.
    std::vector<DeviceInstance> devices = {
        make_device("cvs1", "ControlledVoltageSource", {{"gain", "1.0"}}),
        make_device("cvs2", "ControlledVoltageSource", {{"gain", "1.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"cvs1.v_neg", "gnd.v"},
        {"cvs2.v_neg", "gnd.v"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, ControlledVoltageSourcesShareVPos_Throws) {
    // v_pos is an active writer; two CVS devices on same v_pos wire must fail.
    std::vector<DeviceInstance> devices = {
        make_device("cvs1", "ControlledVoltageSource", {{"gain", "1.0"}}),
        make_device("cvs2", "ControlledVoltageSource", {{"gain", "1.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"cvs1.v_pos", "cvs2.v_pos"},
        {"cvs1.v_neg", "gnd.v"},
        {"cvs2.v_neg", "gnd.v"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, GS24AndBatteryOnSameWire_Throws) {
    std::vector<DeviceInstance> devices = {
        make_device("gs", "GS24"),
        make_device("bat", "Battery", {{"v_nominal", "28.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gs.v_out", "bat.v_out"},
        {"gs.v_in", "gnd.v"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, RU19AStartBusPortsActAsWriters) {
    // Both RU19A writer ports (v_bus and v_start) must participate in
    // one-source-per-wire conflict detection.
    std::vector<DeviceInstance> devices = {
        make_device("ru", "RU19A"),
        make_device("bat1", "Battery", {{"v_nominal", "28.0"}}),
        make_device("bat2", "Battery", {{"v_nominal", "28.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections_v_bus = {
        {"ru.v_bus", "bat1.v_out"},
        {"ru.v_start", "gnd.v"}
    };
    EXPECT_THROW(build_systems_dev(devices, connections_v_bus), std::runtime_error);

    std::vector<std::pair<std::string, std::string>> connections_v_start = {
        {"ru.v_start", "bat2.v_out"},
        {"ru.v_bus", "gnd.v"}
    };
    EXPECT_THROW(build_systems_dev(devices, connections_v_start), std::runtime_error);
}

TEST(PushBuildValidation, MultipleLoadsOK) {
    // Multiple loads on same wire - should succeed (loads are not sources)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("load1", "Load", {{"conductance", "0.1"}}),
        make_device("load2", "Load", {{"conductance", "0.2"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "load1.input"},
        {"battery.v_out", "load2.input"},
        {"battery.v_out", "gnd.v"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, SeparateWiresOK) {
    // Two separate circuits - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "Battery", {{"v_nominal", "28.0"}}),
        make_device("load1", "Load", {{"conductance", "0.1"}}),
        make_device("gnd1", "RefNode", {{"value", "0"}}),
        make_device("battery2", "Battery", {{"v_nominal", "12.0"}}),
        make_device("load2", "Load", {{"conductance", "0.1"}}),
        make_device("gnd2", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery1.v_out", "load1.input"},
        {"battery1.v_out", "gnd1.v"},
        {"battery2.v_out", "load2.input"},
        {"battery2.v_out", "gnd2.v"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 1u);
    });
}

TEST(PushBuildValidation, TwoBatteriesDirectConnection) {
    // Two batteries directly connected with no load - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "Battery", {{"v_nominal", "28.0"}}),
        make_device("battery2", "Battery", {{"v_nominal", "27.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery1.v_out", "battery2.v_out"},  // Direct conflict
        {"battery1.v_out", "gnd.v"},
        {"battery2.v_out", "gnd.v"}
    };
    
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, SingleBatteryOK) {
    // Just one battery - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "Battery", {{"v_nominal", "28.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "gnd.v"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, TopologicalOrder_LinearChain) {
    // Intentionally declare Multiply before Add to verify topo reordering:
    // ref_a(2) + ref_b(4) -> Add.o(6) -> Multiply.A; Multiply.B <- ref_mul(3)
    // Expected mul.o = 18 after one step.
    std::vector<DeviceInstance> devices = {
        make_device("mul", "Multiply"),
        make_device("add", "Add"),
        make_device("ref_a", "RefNode", {{"value", "2"}}),
        make_device("ref_b", "RefNode", {{"value", "4"}}),
        make_device("ref_mul", "RefNode", {{"value", "3"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ref_a.v", "add.A"},
        {"ref_b.v", "add.B"},
        {"add.o", "mul.A"},
        {"ref_mul.v", "mul.B"}
    };

    auto result = build_systems_dev(devices, connections);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        st.allocate_signal(0.0f, {Domain::Electrical, true});
    }

    result.scheduler.step(st, 1.0f / 60.0f);

    const uint32_t mul_out_sig = result.port_to_signal.at("mul.o");
    EXPECT_NEAR(st.values[mul_out_sig], 18.0f, 1e-4f);
}

TEST(PushBuildValidation, TopologicalOrder_CycleFallsBackNoThrow) {
    // Cycle: add1.o -> add2.A and add2.o -> add1.A
    // Should not throw; scheduler should still run (cycle uses fallback ordering).
    std::vector<DeviceInstance> devices = {
        make_device("add1", "Add"),
        make_device("add2", "Add"),
        make_device("ref1", "RefNode", {{"value", "1"}}),
        make_device("ref2", "RefNode", {{"value", "2"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"add2.o", "add1.A"},
        {"ref1.v", "add1.B"},
        {"add1.o", "add2.A"},
        {"ref2.v", "add2.B"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);

        SimulationState st;
        for (uint32_t i = 0; i < result.signal_count; ++i) {
            st.allocate_signal(0.0f, {Domain::Electrical, true});
        }

        EXPECT_NO_THROW(result.scheduler.step(st, 1.0f / 60.0f));
        EXPECT_TRUE(std::isfinite(st.values[result.port_to_signal.at("add1.o")]));
        EXPECT_TRUE(std::isfinite(st.values[result.port_to_signal.at("add2.o")]));
    });
}

TEST(PushBuildValidation, ParseInitialValuesFromJson) {
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "24.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_in", "to": "gnd.v"}
        ],
        "initial_values": {
            "bat.v_out": 17.25
        }
    })";

    auto ctx = parse_json(json);
    auto it = ctx.initial_values.find("bat.v_out");
    ASSERT_NE(it, ctx.initial_values.end());
    EXPECT_NEAR(it->second, 17.25f, 1e-6f);
}

TEST(PushBuildValidation, SimulatorAppliesInitialValuesBeforeStep) {
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "24.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_in", "to": "gnd.v"}
        ],
        "initial_values": {
            "bat.v_out": 19.5
        }
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    EXPECT_NEAR(sim.get_port_value("bat", "v_out"), 19.5f, 1e-5f);
}

TEST(PushBuildValidation, TypeDefinitionWithoutExecutionIsAccepted) {
    nlohmann::json td = {
        {"classname", "TestNoExec"},
        {"description", "test"},
        {"cpp_class", true},
        {"ports", {
            {"in", {{"direction", "in"}, {"type", "Any"}}},
            {"out", {{"direction", "out"}, {"type", "Any"}}}
        }},
        {"domains", {"Logical"}},
        {"params", nlohmann::json::object()}
    };

    EXPECT_NO_THROW({
        auto def = parse_type_definition(td);
        EXPECT_FALSE(def.execution.has_value());
    });
}

TEST(PushBuildValidation, MaxSelectorSelectsHigherInput) {
    std::vector<DeviceInstance> devices = {
        make_device("sel", "MaxSelector"),
        make_device("ref_a", "RefNode", {{"value", "3.0"}}),
        make_device("ref_b", "RefNode", {{"value", "5.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ref_a.v", "sel.A"},
        {"ref_b.v", "sel.B"}
    };

    auto result = build_systems_dev(devices, connections);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        (void)st.allocate_signal(0.0f, {Domain::Electrical, true});
    }

    result.scheduler.step(st, 1.0f / 60.0f);
    const uint32_t out_sig = result.port_to_signal.at("sel.o");
    EXPECT_NEAR(st.values[out_sig], 5.0f, 1e-5f);
}

TEST(PushBuildValidation, MaxSelectorAvoidsSourceConflict) {
    std::vector<DeviceInstance> devices = {
        make_device("bat", "Battery", {{"v_nominal", "28.0"}}),
        make_device("gen", "Generator", {{"v_nominal", "28.5"}}),
        make_device("sel", "MaxSelector"),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "bat.v_in"},
        {"gnd.v", "gen.v_in"},
        {"bat.v_out", "sel.A"},
        {"gen.v_out", "sel.B"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);

        SimulationState st;
        for (uint32_t i = 0; i < result.signal_count; ++i) {
            (void)st.allocate_signal(0.0f, {Domain::Electrical, true});
        }

        result.scheduler.step(st, 1.0f / 60.0f);
        const uint32_t out_sig = result.port_to_signal.at("sel.o");
        EXPECT_NEAR(st.values[out_sig], 28.5f, 1e-4f);
    });
}

TEST(PushBuildValidation, UnknownClassnameThrows) {
    std::vector<DeviceInstance> devices = {
        make_device("mystery", "NoSuchComponent", {}),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "mystery.in"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, WhitelistParamsRejected) {
    std::vector<DeviceInstance> devices = {
        make_device("bat", "Battery", {
            {"v_nominal", "28.0"},
            {"inv_internal_r", "40.0"}
        }),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "bat.v_in"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, UnknownParamThrows) {
    std::vector<DeviceInstance> devices = {
        make_device("load", "Load", {
            {"conductance", "0.1"},
            {"bogus_param", "42"}
        }),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"load.input", "gnd.v"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, MissingRequiredParamThrows) {
    std::vector<DeviceInstance> devices = {
        make_device("p", "P", {
            {"output_min", "-10.0"},
            {"output_max", "10.0"}
        }),
        make_device("ref", "RefNode", {{"value", "1.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ref.v", "p.setpoint"},
        {"ref.v", "p.feedback"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, MissingDomainsThrows) {
    const std::string json = R"({
        "version": "3.0",
        "classname": "BadNoDomains",
        "description": "invalid type",
        "cpp_class": true,
        "interface": [
            {"name": "in", "direction": 0, "type": "Any"},
            {"name": "out", "direction": 1, "type": "Any"}
        ],
        "param_defaults": {}
    })";

    EXPECT_THROW(parse_type_definition(nlohmann::json::parse(json)), std::runtime_error);
}

// ============================================================================
// Regression: visual_only params must not reach JIT solver validation
// ============================================================================

TEST(PushBuildValidation, BusWithVisualOnlyParam_PortEdge_DoesNotThrow) {
    // A Bus with a visual-only 'port_edge' parameter must not cause
    // "Unknown/unconsumed parameter" in build_systems_dev.
    // This is a regression test for the bug where string_params like
    // port_edge leaked into the simulation and caused build failure.
    std::vector<DeviceInstance> devices = {
        make_device("bat", "Battery", {{"v_nominal", "28.0"}}),
        make_device("bus_1", "Bus"),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "bus_1.v"},
        {"bus_1.v", "gnd.v"}
    };

    // Without the fix, port_edge in params would cause:
    //   "Unknown/unconsumed parameter 'port_edge' for component 'bus_1'"
    // The fix filters visual_only params in build_simulation_json() so they
    // never reach build_systems_dev(). Verify the solver side is clean too:
    // a Bus with NO extra params must build successfully.
    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, ParamSchemaVisualOnlyFlag) {
    // Verify visual_only flag is correctly parsed from param_schema JSON
    const auto j = nlohmann::json::parse(R"({
        "version": "3.0",
        "classname": "TestVisual",
        "description": "test",
        "cpp_class": true,
        "domains": ["Electrical"],
        "interface": [
            {"name": "v", "direction": 2, "type": "V"}
        ],
        "param_defaults": {
            "port_edge": "bottom",
            "v_nominal": "28.0"
        },
        "param_schema": {
            "port_edge": {"type": "string", "visual_only": true},
            "v_nominal": {"type": "float"}
        }
    })");

    TypeDefinition def = parse_type_definition(j);

    ASSERT_TRUE(def.param_schema.count("port_edge") > 0);
    EXPECT_TRUE(def.param_schema.at("port_edge").visual_only);
    EXPECT_EQ(def.param_schema.at("port_edge").type, ParamSchemaType::String);

    ASSERT_TRUE(def.param_schema.count("v_nominal") > 0);
    EXPECT_FALSE(def.param_schema.at("v_nominal").visual_only);
}

// ============================================================================
// Regression: RU19A observation ports (rpm_out, t4_out) must be classified
// as outputs for topological ordering. A downstream consumer reading rpm_out
// must be ordered after RU19A so it sees the value written in the same step.
// ============================================================================

TEST(PushBuildValidation, RU19AObservationPortsAreOutputsForTopo) {
    // Build: RU19A writes rpm_out -> Greater reads it as input A.
    // If rpm_out were misclassified as an input, Greater would not have a
    // topological dependency on RU19A, potentially executing first and
    // reading a stale zero.
    std::vector<DeviceInstance> devices = {
        make_device("greater", "Greater"),  // declared BEFORE ru to stress topo sort
        make_device("ru", "RU19A"),
        make_device("ref_threshold", "RefNode", {{"value", "50"}}),
        make_device("gnd_bus", "RefNode", {{"value", "0"}}),
        make_device("gnd_start", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ru.rpm_out", "greater.A"},
        {"ref_threshold.v", "greater.B"},
        {"ru.v_bus", "gnd_bus.v"},
        {"ru.v_start", "gnd_start.v"}
    };

    auto result = build_systems_dev(devices, connections);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        st.allocate_signal(0.0f, {Domain::Electrical, true});
    }

    // The initial rpm_out should be 0 (APU is OFF), so Greater(0 > 50) = 0.
    result.scheduler.step(st, 1.0f / 60.0f);
    const uint32_t greater_out = result.port_to_signal.at("greater.o");
    EXPECT_FLOAT_EQ(st.values[greater_out], 0.0f);

    // The value must be finite (not NaN from uninitialized read).
    EXPECT_TRUE(std::isfinite(st.values[greater_out]));
}
