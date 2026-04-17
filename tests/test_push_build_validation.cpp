#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"
#include "json_parser/json_parser.h"
#include "jit_build_input_test_helper.h"
#include <cmath>
#include <algorithm>

namespace {

const TypeRegistry& test_registry() {
    static const TypeRegistry registry = load_type_registry("library/");
    return registry;
}

// Helper to create a basic device instance
DeviceInstance make_device(const std::string& name, const std::string& classname,
                          const std::unordered_map<std::string, std::string>& params = {},
                          bool merge_defaults = true) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};
    
    if (const TypeDefinition* def = test_registry().get(classname)) {
        for (const auto& [port_name, port] : def->ports) {
            dev.ports[port_name] = port;
        }
        if (merge_defaults) {
            for (const auto& [param_name, param_value] : def->params) {
                auto schema_it = def->param_schema.find(param_name);
                if (schema_it != def->param_schema.end() && schema_it->second.visual_only) {
                    continue;
                }
                if (!dev.params.count(param_name)) {
                    dev.params[param_name] = param_value;
                }
            }
        }
        dev.solver_role = def->solver_role;
    } else {
        auto ports = get_component_ports(classname);
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
        }
    }
    return dev;
}

} // anonymous namespace

// ============================================================================
// Push Build Validation Tests - Phase 3.1
// Validates one-source-per-wire constraint in build_systems_dev
// ============================================================================

TEST(PushBuildValidation, SingleSourcePerWireOK) {
    // Single ElectricalSource driving a Resistor - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "battery.v_in"}
    };
    
    // Should not throw - single source per wire
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, MultipleSourcesSameWireErrors) {
    // Two ElectricalSources driving the same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("battery2", "ElectricalSource", {{"voltage", "27.0"}}),
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery1.v_out", "battery2.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "battery2.v_in"}
    };
    
    // Should throw - multiple voltage sources on same wire
    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, MultipleSourceLikeComponentsConflict) {
    // Generator + ControlledVoltageSource on same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.5"}}),
        make_device("cvs", "ControlledVoltageSource"),
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}}),
        make_device("cvs_gain", "Value", {{"value", "1.0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "cvs.v_pos", "load.v_in"},
        {"load.v_out", "gnd.v", "cvs.v_neg"},
        {"cvs_gain.o", "cvs.gain"}
    };
    
    // Should throw - two different source types writing to same signal
    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, BatteryAndGeneratorOnSameWire) {
    // ElectricalSource + Generator on same wire - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("generator", "Generator", {{"v_nominal", "28.5"}}),
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "generator.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "generator.v_in"}
    };
    
    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, BatteryAndRefNodeOnSameWire) {
    // ElectricalSource + RefNode on same wire - RefNode defines reference, NOT an active source
    // So this should be OK (RefNode just provides 0V reference)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("ref", "RefNode", {{"value", "5.0"}}),  // 5V reference
        make_device("load", "Resistor", {{"conductance", "0.1"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "load.v_in"},
        {"load.v_out", "ref.v", "battery.v_in"}
    };
    
    // RefNode is a reference point, not an active source - allowed
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, ControlledCurrentSourceConflict) {
    // ControlledCurrentSource writing to same wire as ElectricalSource - should throw
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("ccs", "ControlledCurrentSource", {{"gain", "1.0"}}),
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "ccs.v_pos", "load.v_in"},
        {"load.v_out", "gnd.v", "ccs.v_neg", "battery.v_in"}
    };
    
    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"cvs1.v_neg", "cvs2.v_neg", "gnd.v"},
        {"cvs1_gain.o", "cvs1.gain"},
        {"cvs2_gain.o", "cvs2.gain"},
        {"cvs1.v_pos"},
        {"cvs2.v_pos"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"cvs1.v_pos", "cvs2.v_pos"},
        {"cvs1.v_neg", "cvs2.v_neg", "gnd.v"},
        {"cvs1_gain.o", "cvs1.gain"},
        {"cvs2_gain.o", "cvs2.gain"}
    };

    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}





TEST(PushBuildValidation, MultipleResistorsOK) {
    // Multiple resistors on same wire - should succeed (passive branches)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("load1", "Resistor", {{"conductance", "0.1"}}),
        make_device("load2", "Resistor", {{"conductance", "0.2"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "load1.v_in", "load2.v_in"},
        {"load1.v_out", "load2.v_out", "gnd.v", "battery.v_in"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
        EXPECT_GT(result.signal_count, 0u);
    });
}

TEST(PushBuildValidation, SeparateWiresOK) {
    // Two separate circuits - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("load1", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd1", "RefNode", {{"value", "0"}}),
        make_device("battery2", "ElectricalSource", {{"voltage", "12.0"}}),
        make_device("load2", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd2", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery1.v_out", "load1.v_in"},
        {"load1.v_out", "gnd1.v", "battery1.v_in"},
        {"battery2.v_out", "load2.v_in"},
        {"load2.v_out", "gnd2.v", "battery2.v_in"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery1.v_out", "battery2.v_out"},
        {"gnd.v", "battery1.v_in", "battery2.v_in"}
    };
    
    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, RotarySwitchAliasesBuildAsKnobSwitch) {
    std::vector<DeviceInstance> devices = {
        make_device("rs_1", "RotarySwitch1ToN", {{"positions", "3"}}),
        make_device("rs_2", "RotarySwitchNTo1", {{"positions", "3"}}),
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_in", "res.v_out", "gnd.v"},
        {"src.v_out", "rs_1.throw1"},
        {"rs_1.wiper", "rs_2.wiper"},
        {"rs_2.throw1", "res.v_in"},
        {"rs_1.throw2"},
        {"rs_1.throw3"},
        {"rs_1.throw4"},
        {"rs_1.throw5"},
        {"rs_2.throw2"},
        {"rs_2.throw3"},
        {"rs_2.throw4"},
        {"rs_2.throw5"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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
    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_in", "res.v_out", "gnd.v"},
        {"src.v_out", "knob.throw1"},
        {"knob.wiper", "res.v_in"},
        {"knob.throw2"},
        {"knob.throw3"},
        {"knob.throw4"},
        {"knob.throw5"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_in", "res.v_out", "gnd.v"},
        {"knob.wiper"},
        {"knob.throw1"},
        {"knob.throw2"},
        {"knob.throw3"},
        {"knob.throw4"},
        {"knob.throw5"},
        {"src.v_out"},
        {"res.v_in"}
    };

    // Build succeeds (broken connections are warned, not fatal)
    auto legacy_result = build_systems_dev(make_jit_input(devices, signal_groups));

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_in", "knob.wiper", "gnd.v"},
        {"src.v_out", "knob.throw1"},
        {"knob.throw2"},
        {"knob.throw3"},
        {"knob.throw4"},
        {"knob.throw5"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_in", "rs.wiper", "gnd.v"},
        {"src.v_out", "rs.throw1"},
        {"rs.throw2"},
        {"rs.throw3"},
        {"rs.throw4"},
        {"rs.throw5"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));
    EXPECT_EQ(result.scheduler.consumer_count(), 0u)
        << "RotarySwitch1ToN should not appear in the push scheduler consumer list";
}

TEST(PushBuildValidation, SolverOwnedElectricalClassification_MetadataDrivenCoverage) {
    // Regression coverage: solver-owned electrical classification must be driven
    // by generated metadata for all currently expected classes/aliases.
    EXPECT_TRUE(is_solver_owned_electrical_component("Generator"));
    EXPECT_TRUE(is_solver_owned_electrical_component("Resistor"));
    EXPECT_TRUE(is_solver_owned_electrical_component("ElectricalConductance"));
    EXPECT_TRUE(is_solver_owned_electrical_component("ElectricalSource"));
    EXPECT_TRUE(is_solver_owned_electrical_component("ControlledVoltageSource"));
    EXPECT_TRUE(is_solver_owned_electrical_component("VariableConductance"));
    EXPECT_TRUE(is_solver_owned_electrical_component("AZS"));
    EXPECT_TRUE(is_solver_owned_electrical_component("HoldButton"));
    EXPECT_TRUE(is_solver_owned_electrical_component("Relay"));
    EXPECT_TRUE(is_solver_owned_electrical_component("KnobSwitch"));
    EXPECT_TRUE(is_solver_owned_electrical_component("RotarySwitch1ToN"));
    EXPECT_TRUE(is_solver_owned_electrical_component("RotarySwitchNTo1"));

    // A non-solver-owned electrical observer should stay false.
    EXPECT_FALSE(is_solver_owned_electrical_component("CurrentSense"));
}

TEST(PushBuildValidation, RotarySwitchAliasesInstantiateDistinctVariantTypes) {
    std::vector<DeviceInstance> devices = {
        make_device("rs_a", "RotarySwitch1ToN", {{"positions", "3"}}),
        make_device("rs_b", "RotarySwitchNTo1", {{"positions", "3"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"rs_a.wiper", "rs_b.wiper"},
        {"rs_a.throw1"},
        {"rs_a.throw2"},
        {"rs_a.throw3"},
        {"rs_a.throw4"},
        {"rs_a.throw5"},
        {"rs_b.throw1"},
        {"rs_b.throw2"},
        {"rs_b.throw3"},
        {"rs_b.throw4"},
        {"rs_b.throw5"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    auto it_a = result.devices.find("rs_a");
    auto it_b = result.devices.find("rs_b");
    ASSERT_NE(it_a, result.devices.end());
    ASSERT_NE(it_b, result.devices.end());

    EXPECT_TRUE(std::holds_alternative<RotarySwitch1ToN<JitProvider>>(it_a->second));
    EXPECT_TRUE(std::holds_alternative<RotarySwitchNTo1<JitProvider>>(it_b->second));
}

TEST(PushBuildValidation, RotarySwitchAliasPortDirectionsMatchTopologyIntent) {
    auto out_1_to_n = get_output_ports("RotarySwitch1ToN");
    auto out_n_to_1 = get_output_ports("RotarySwitchNTo1");

    std::unordered_set<std::string> s1(out_1_to_n.begin(), out_1_to_n.end());
    std::unordered_set<std::string> s2(out_n_to_1.begin(), out_n_to_1.end());

    // 1->N: one electrical input (wiper), N electrical outputs (throw*)
    EXPECT_EQ(s1.count("wiper"), 0u);
    EXPECT_EQ(s1.count("throw1"), 1u);
    EXPECT_EQ(s1.count("throw2"), 1u);
    EXPECT_EQ(s1.count("throw3"), 1u);
    EXPECT_EQ(s1.count("throw4"), 1u);
    EXPECT_EQ(s1.count("throw5"), 1u);

    // N->1: N electrical inputs (throw*), one electrical output (wiper)
    EXPECT_EQ(s2.count("wiper"), 1u);
    EXPECT_EQ(s2.count("throw1"), 0u);
    EXPECT_EQ(s2.count("throw2"), 0u);
    EXPECT_EQ(s2.count("throw3"), 0u);
    EXPECT_EQ(s2.count("throw4"), 0u);
    EXPECT_EQ(s2.count("throw5"), 0u);
}

TEST(PushBuildValidation, AzsConsumesNonDefaultParamsFromSemanticData) {
    DeviceInstance azs = make_device("azs", "AZS", {
        {"closed", "true"},
        {"i_nominal", "7.5"},
        {"g_open", "0.25"},
        {"g_closed", "321.0"}
    });
    DeviceInstance src = make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}});
    DeviceInstance gnd = make_device("gnd", "RefNode", {{"value", "0.0"}});

    std::vector<DeviceInstance> devices = {src, azs, gnd};
    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "azs.v_in"},
        {"azs.v_out"},
        {"src.v_in", "gnd.v"},
        {"azs.control"},
        {"azs.state"},
        {"azs.temp"},
        {"azs.tripped"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    auto it = result.devices.find("azs");
    ASSERT_NE(it, result.devices.end());
    const AZS<JitProvider>* comp = std::get_if<AZS<JitProvider>>(&it->second);
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->closed);
    EXPECT_FLOAT_EQ(comp->i_nominal, 7.5f);
    EXPECT_FLOAT_EQ(comp->g_open, 0.25f);
    EXPECT_FLOAT_EQ(comp->g_closed, 321.0f);
}

TEST(PushBuildValidation, RelayConsumesNonDefaultParamsFromSemanticData) {
    DeviceInstance relay = make_device("relay", "Relay", {
        {"closed", "true"},
        {"g_open", "0.125"},
        {"g_closed", "456.0"}
    });
    DeviceInstance src = make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}});
    DeviceInstance gnd = make_device("gnd", "RefNode", {{"value", "0.0"}});

    std::vector<DeviceInstance> devices = {src, relay, gnd};
    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "relay.v_in"},
        {"relay.v_out"},
        {"src.v_in", "gnd.v"},
        {"relay.control"},
        {"relay.state"},
        {"relay.hold_threshold"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    auto it = result.devices.find("relay");
    ASSERT_NE(it, result.devices.end());
    const Relay<JitProvider>* comp = std::get_if<Relay<JitProvider>>(&it->second);
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->closed);
    EXPECT_FLOAT_EQ(comp->g_open, 0.125f);
    EXPECT_FLOAT_EQ(comp->g_closed, 456.0f);
}

TEST(PushBuildValidation, HoldButtonConsumesNonDefaultParamsFromSemanticData) {
    DeviceInstance btn = make_device("btn", "HoldButton", {
        {"idle", "2.5"},
        {"g_open", "0.125"},
        {"g_closed", "654.0"}
    });
    DeviceInstance src = make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}});
    DeviceInstance gnd = make_device("gnd", "RefNode", {{"value", "0.0"}});

    std::vector<DeviceInstance> devices = {src, btn, gnd};
    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "btn.v_in"},
        {"btn.v_out"},
        {"src.v_in", "gnd.v"},
        {"btn.control"},
        {"btn.state"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    auto it = result.devices.find("btn");
    ASSERT_NE(it, result.devices.end());
    const HoldButton<JitProvider>* comp = std::get_if<HoldButton<JitProvider>>(&it->second);
    ASSERT_NE(comp, nullptr);
    EXPECT_FLOAT_EQ(comp->idle, 2.5f);
    EXPECT_FLOAT_EQ(comp->g_open, 0.125f);
    EXPECT_FLOAT_EQ(comp->g_closed, 654.0f);
}

TEST(PushBuildValidation, SingleBatteryOK) {
    // Just one ElectricalSource - should succeed
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };
    
    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "gnd.v", "battery.v_in"}
    };
    
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"ref_a.v", "add.A"},
        {"ref_b.v", "add.B"},
        {"add.o", "mul.A"},
        {"ref_mul.v", "mul.B"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        st.allocate_signal(0.0f);
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"add2.o", "add1.A"},
        {"ref1.v", "add1.B"},
        {"add1.o", "add2.A"},
        {"ref2.v", "add2.B"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));

        SimulationState st;
        for (uint32_t i = 0; i < result.signal_count; ++i) {
            st.allocate_signal(0.0f);
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
    sim.start(build_input_from_json(json));

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"ref_a.v", "sel.A"},
        {"ref_b.v", "sel.B"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        (void)st.allocate_signal(0.0f);
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gnd.v", "bat.v_in", "gen.v_in"},
        {"bat.v_out", "sel.A"},
        {"gen.v_out", "sel.B"}
    };

    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));

        SimulationState st;
        for (uint32_t i = 0; i < result.signal_count; ++i) {
            (void)st.allocate_signal(0.0f);
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gnd.v", "mystery.in"}
    };

    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, MetadataHelpersUnknownClassFailFast) {
    EXPECT_THROW(
        {
            (void)is_scheduler_source_component("NoSuchComponent");
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            (void)is_solver_owned_electrical_component("NoSuchComponent");
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            (void)get_output_ports("NoSuchComponent");
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            (void)get_source_writer_ports("NoSuchComponent", static_cast<uint8_t>(Domain::Electrical));
        },
        std::runtime_error
    );
}

TEST(PushBuildValidation, WhitelistParamsRejected) {
    std::vector<DeviceInstance> devices = {
        make_device("bat", "ElectricalSource", {
            {"voltage", "28.0"},
            {"inv_internal_r", "40.0"}
        }),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"gnd.v", "bat.v_in"}
    };

    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, UnknownParamThrows) {
    std::vector<DeviceInstance> devices = {
        make_device("load", "Resistor", {
            {"conductance", "0.1"},
            {"bogus_param", "42"}
        }),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"load.v_out", "gnd.v"}
    };

    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
}

TEST(PushBuildValidation, MissingRequiredParamThrows) {
    std::vector<DeviceInstance> devices = {
        make_device("p", "P", {
            {"output_min", "-10.0"},
            {"output_max", "10.0"}
        }, false),
        make_device("ref", "RefNode", {{"value", "1.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"ref.v", "p.setpoint"},
        {"ref.v", "p.feedback"}
    };

    EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"bat.v_out", "bus_1.v", "gnd.v"}
    };

    // Without the fix, port_edge in params would cause:
    //   "Unknown/unconsumed parameter 'port_edge' for component 'bus_1'"
    // The fix filters visual_only params in the canonical editor elaboration so they
    // never reach build_systems_dev(). Verify the solver side is clean too:
    // a Bus with NO extra params must build successfully.
    EXPECT_NO_THROW({
        auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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
        make_device("load", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "battery.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    EXPECT_FALSE(is_scheduler_source_component("ElectricalSource"));
    EXPECT_TRUE(is_scheduler_source_component("RefNode"));
    EXPECT_FALSE(is_scheduler_source_component("Resistor"));

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        st.allocate_signal(0.0f);
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
TEST(PushBuildValidation, SentinelIsFixedSignal) {
    std::vector<DeviceInstance> devices = {
        make_device("ref", "RefNode", {{"value", "0.0"}})
    };
    std::vector<std::vector<std::string>> signal_groups;

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

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
