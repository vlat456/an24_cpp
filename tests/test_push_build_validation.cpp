#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include "json_parser/json_parser.h"
#include <cmath>
#include <algorithm>

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
    // Single ElectricalSource driving a Load - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
    // Two ElectricalSources driving the same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("battery2", "ElectricalSource", {{"voltage", "27.0"}}),
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
        make_device("cvs", "ControlledVoltageSource"),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}}),
        make_device("cvs_gain", "Value", {{"value", "1.0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "cvs.v_pos"},  // Both writing to same wire
        {"gen.v_out", "load.input"},
        {"cvs.v_neg", "gnd.v"},
        {"cvs_gain.o", "cvs.gain"}
    };
    
    // Should throw - two different source types writing to same signal
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, BatteryAndGeneratorOnSameWire) {
    // ElectricalSource + Generator on same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
    // ElectricalSource + RefNode on same wire - RefNode defines reference, NOT an active source
    // So this should be OK (RefNode just provides 0V reference)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
    // ControlledCurrentSource writing to same wire as ElectricalSource - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
        make_device("cvs1", "ControlledVoltageSource"),
        make_device("cvs2", "ControlledVoltageSource"),
        make_device("gnd", "RefNode", {{"value", "0"}}),
        make_device("cvs1_gain", "Value", {{"value", "1.0"}}),
        make_device("cvs2_gain", "Value", {{"value", "1.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"cvs1.v_neg", "gnd.v"},
        {"cvs2.v_neg", "gnd.v"},
        {"cvs1_gain.o", "cvs1.gain"},
        {"cvs2_gain.o", "cvs2.gain"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, ControlledVoltageSourcesShareVPos_Throws) {
    // v_pos is an active writer; two CVS devices on same v_pos wire must fail.
    std::vector<DeviceInstance> devices = {
        make_device("cvs1", "ControlledVoltageSource"),
        make_device("cvs2", "ControlledVoltageSource"),
        make_device("gnd", "RefNode", {{"value", "0"}}),
        make_device("cvs1_gain", "Value", {{"value", "1.0"}}),
        make_device("cvs2_gain", "Value", {{"value", "1.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"cvs1.v_pos", "cvs2.v_pos"},
        {"cvs1.v_neg", "gnd.v"},
        {"cvs2.v_neg", "gnd.v"},
        {"cvs1_gain.o", "cvs1.gain"},
        {"cvs2_gain.o", "cvs2.gain"}
    };

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}





TEST(PushBuildValidation, MultipleLoadsOK) {
    // Multiple loads on same wire - should succeed (loads are not sources)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("load1", "Load", {{"conductance", "0.1"}}),
        make_device("gnd1", "RefNode", {{"value", "0"}}),
        make_device("battery2", "ElectricalSource", {{"voltage", "12.0"}}),
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
    // Two ElectricalSources directly connected with no load - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("battery2", "ElectricalSource", {{"voltage", "27.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery1.v_out", "battery2.v_out"},  // Direct conflict
        {"battery1.v_out", "gnd.v"},
        {"battery2.v_out", "gnd.v"}
    };
    
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(PushBuildValidation, RotarySwitchAliasesBuildAsKnobSwitch) {
    std::vector<DeviceInstance> devices = {
        make_device("rs_1", "RotarySwitch1ToN", {{"positions", "3"}}),
        make_device("rs_2", "RotarySwitchNTo1", {{"positions", "3"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"src.v_in", "gnd.v"},
        {"src.v_out", "rs_1.throw1"},
        {"rs_1.wiper", "rs_2.wiper"},
        {"rs_2.throw1", "res.v_in"},
        {"res.v_out", "gnd.v"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(devices, connections);
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, KnobSwitchPortNamesAreWiperAndThrowsOnly) {
    std::vector<DeviceInstance> devices = {
        make_device("knob", "KnobSwitch", {{"positions", "2"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    // New port names build and connect correctly
    std::vector<std::pair<std::string, std::string>> new_connections = {
        {"src.v_in", "gnd.v"},
        {"src.v_out", "knob.throw1"},
        {"knob.wiper", "res.v_in"},
        {"res.v_out", "gnd.v"}
    };

    auto result = build_systems_dev(devices, new_connections);
    EXPECT_GT(result.signal_count, 0u);

    // Verify new port names exist
    EXPECT_EQ(result.port_to_signal.count("knob.wiper"), 1u);
    EXPECT_EQ(result.port_to_signal.count("knob.throw1"), 1u);

    // Verify new names are actually connected (unified with src/res ports)
    EXPECT_EQ(result.port_to_signal["knob.throw1"],
              result.port_to_signal["src.v_out"]);
    EXPECT_EQ(result.port_to_signal["knob.wiper"],
              result.port_to_signal["res.v_in"]);

    // Legacy port names (common, t1..t5) do NOT exist as device ports
    EXPECT_EQ(result.port_to_signal.count("knob.common"), 0u);
    EXPECT_EQ(result.port_to_signal.count("knob.t1"), 0u);
}

TEST(PushBuildValidation, KnobSwitchLegacyPortNamesAreNotConnected) {
    // Verify that legacy port names in connections silently fail to connect.
    // Legacy names ("common", "t1") are not in the port registry and don't
    // create port entries; connections referencing them are ignored with a warning.
    std::vector<DeviceInstance> devices = {
        make_device("knob", "KnobSwitch", {{"positions", "2"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> legacy_connections = {
        {"src.v_in", "gnd.v"},
        {"src.v_out", "knob.t1"},         // legacy name — no such port
        {"knob.common", "res.v_in"},      // legacy name — no such port
        {"res.v_out", "gnd.v"}
    };

    // Build succeeds (broken connections are warned, not fatal)
    auto legacy_result = build_systems_dev(devices, legacy_connections);

    // The new ports exist (from device port metadata) but are NOT connected to src/res
    EXPECT_EQ(legacy_result.port_to_signal.count("knob.wiper"), 1u);
    EXPECT_EQ(legacy_result.port_to_signal.count("knob.throw1"), 1u);
    EXPECT_NE(legacy_result.port_to_signal["knob.throw1"],
              legacy_result.port_to_signal["src.v_out"])
        << "Legacy connection 'knob.t1' should NOT unify with 'src.v_out'";
    EXPECT_NE(legacy_result.port_to_signal["knob.wiper"],
              legacy_result.port_to_signal["res.v_in"])
        << "Legacy connection 'knob.common' should NOT unify with 'res.v_in'";
}

TEST(PushBuildValidation, KnobSwitchIsNotInPushScheduler) {
    // Regression: KnobSwitch is solver-owned and must NOT be added to the push
    // scheduler as a consumer. Previously it was missing from
    // is_solver_owned_electrical_propagator(), causing double commit() calls.
    std::vector<DeviceInstance> devices = {
        make_device("knob", "KnobSwitch", {{"positions", "2"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"src.v_in", "gnd.v"},
        {"src.v_out", "knob.throw1"},
        {"knob.wiper", "gnd.v"}
    };

    auto result = build_systems_dev(devices, connections);
    // KnobSwitch should NOT be a scheduler consumer
    // RefNode is a source, ElectricalSource/Resistor are solver-owned — so zero consumers expected
    EXPECT_EQ(result.scheduler.consumer_count(), 0u)
        << "KnobSwitch should not appear in the push scheduler consumer list";
}

TEST(PushBuildValidation, RotarySwitchAliasesAreNotInPushScheduler) {
    // Same guardrail check for RotarySwitch aliases
    std::vector<DeviceInstance> devices = {
        make_device("rs", "RotarySwitch1ToN", {{"positions", "3"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"src.v_in", "gnd.v"},
        {"src.v_out", "rs.throw1"},
        {"rs.wiper", "gnd.v"}
    };

    auto result = build_systems_dev(devices, connections);
    EXPECT_EQ(result.scheduler.consumer_count(), 0u)
        << "RotarySwitch1ToN should not appear in the push scheduler consumer list";
}

TEST(PushBuildValidation, RotarySwitchAliasesInstantiateDistinctVariantTypes) {
    std::vector<DeviceInstance> devices = {
        make_device("rs_a", "RotarySwitch1ToN", {{"positions", "3"}}),
        make_device("rs_b", "RotarySwitchNTo1", {{"positions", "3"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"rs_a.wiper", "rs_b.wiper"}
    };

    auto result = build_systems_dev(devices, connections);

    auto it_a = result.devices.find("rs_a");
    auto it_b = result.devices.find("rs_b");
    ASSERT_NE(it_a, result.devices.end());
    ASSERT_NE(it_b, result.devices.end());

    EXPECT_TRUE(std::holds_alternative<RotarySwitch1ToN<JitProvider>>(it_a->second));
    EXPECT_TRUE(std::holds_alternative<RotarySwitchNTo1<JitProvider>>(it_b->second));
}

TEST(PushBuildValidation, SingleBatteryOK) {
    // Just one ElectricalSource - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
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
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "24.0"}},
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
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "24.0"}},
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

TEST(PushBuildValidation, MaxSelectsHigherInput) {
    std::vector<DeviceInstance> devices = {
        make_device("sel", "Max"),
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

TEST(PushBuildValidation, MaxAvoidsSourceConflict) {
    std::vector<DeviceInstance> devices = {
        make_device("bat", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("gen", "Generator", {{"v_nominal", "28.5"}}),
        make_device("sel", "Max"),
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

        // Battery and Generator are solver-owned; their execute() does not run
        // via the push scheduler. Seed the output voltages manually (as the
        // electrical solver would in a full simulation).
        st.values[result.port_to_signal.at("bat.v_out")] = 28.0f;
        st.values[result.port_to_signal.at("gen.v_out")] = 28.5f;

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
        make_device("bat", "ElectricalSource", {
            {"voltage", "28.0"},
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
        make_device("bat", "ElectricalSource", {{"voltage", "28.0"}}),
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



TEST(PushBuildValidation, SchedulerSourceMetadata_ControlsBucketing) {
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("load", "Load", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "load.input"},
        {"battery.v_out", "gnd.v"}
    };

    auto result = build_systems_dev(devices, connections);

    EXPECT_FALSE(is_scheduler_source_component("ElectricalSource"));
    EXPECT_TRUE(is_scheduler_source_component("RefNode"));
    EXPECT_FALSE(is_scheduler_source_component("Load"));

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        st.allocate_signal(0.0f, {Domain::Electrical, true});
    }
    EXPECT_NO_THROW(result.scheduler.step(st, 1.0f / 60.0f));
}





// Regression: merge_device_instance must propagate domain and source_writer
// from the type definition when both instance and definition have the same port.
// Previously only type and alias were copied, silently dropping metadata.
TEST(PushBuildValidation, MergeDeviceInstance_PropagatesPortDomainAndSourceWriter) {
    TypeDefinition def;
    def.classname = "Generator";
    def.cpp_class = true;
    def.domains = std::vector<Domain>{Domain::Electrical};
    // Definition port: domain=Mechanical, source_writer=true
    def.ports["v_out"] = Port{PortDirection::Out, PortType::V, Domain::Mechanical, true};

    DeviceInstance inst;
    inst.name = "gen1";
    inst.classname = "Generator";
    // Instance port: same name, but with default domain/source_writer
    inst.ports["v_out"] = Port{PortDirection::Out, PortType::V};

    DeviceInstance merged = merge_device_instance(inst, def);

    // domain and source_writer must come from the definition, not remain at defaults
    EXPECT_EQ(merged.ports.at("v_out").domain, Domain::Mechanical);
    EXPECT_TRUE(merged.ports.at("v_out").source_writer);
}

// Regression: parse_type_definition must parse scheduler_source from JSON.
// Previously it was missing, always defaulting to false even when JSON said true.
TEST(PushBuildValidation, ParseTypeDefinition_ParsesSchedulerSource) {
    auto j = nlohmann::json::parse(R"({
        "classname": "TestSource",
        "cpp_class": true,
        "scheduler_source": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}}
    })");

    TypeDefinition def = parse_type_definition(j);
    EXPECT_TRUE(def.scheduler_source);

    // Also verify false case
    auto j2 = nlohmann::json::parse(R"({
        "classname": "TestLoad",
        "cpp_class": true,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })");

    TypeDefinition def2 = parse_type_definition(j2);
    EXPECT_FALSE(def2.scheduler_source);
}

// Regression: sentinel signal must be included in fixed_signals.
// Sentinel is conceptually fixed — always allocated at end, never changes.
// Previously it was counted as dynamic, making dynamic_signals_count imprecise.
TEST(PushBuildValidation, SentinelIsFixedSignal) {
    std::vector<DeviceInstance> devices = {
        make_device("ref", "RefNode", {{"value", "0.0"}})
    };
    std::vector<std::pair<std::string, std::string>> connections;

    auto result = build_systems_dev(devices, connections);

    uint32_t sentinel = result.signal_count - 1;
    bool sentinel_in_fixed = false;
    for (uint32_t fs : result.fixed_signals) {
        if (fs == sentinel) {
            sentinel_in_fixed = true;
            break;
        }
    }
    EXPECT_TRUE(sentinel_in_fixed) << "Sentinel signal (index=" << sentinel << ") must be in fixed_signals";
}
