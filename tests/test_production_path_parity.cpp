#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "codegen/codegen.h"
#include <set>

namespace {

ExecutionPhases make_execution(
    bool electrical_passive,
    bool electrical_observer,
    bool logical,
    bool control_commit,
    bool electrical_actuator,
    bool finalize,
    bool mechanical,
    bool hydraulic,
    bool thermal) {
    ExecutionPhases phases;
    phases.electrical_passive = electrical_passive;
    phases.electrical_observer = electrical_observer;
    phases.logical = logical;
    phases.control_commit = control_commit;
    phases.electrical_actuator = electrical_actuator;
    phases.finalize = finalize;
    phases.mechanical = mechanical;
    phases.hydraulic = hydraulic;
    phases.thermal = thermal;
    return phases;
}

TypeRegistry build_registry_for_lamp() {
    TypeRegistry registry;

    TypeDefinition bp_in;
    bp_in.classname = "BlueprintInput";
    bp_in.cpp_class = true;
    bp_in.ports["port"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    bp_in.ports["ext"] = Port{PortDirection::In, PortType::Any, std::string("port")};
    bp_in.domains = {{Domain::Electrical}};
    bp_in.execution = make_execution(true, false, true, false, false, false, true, true, true);
    registry.types["BlueprintInput"] = bp_in;

    TypeDefinition bp_out;
    bp_out.classname = "BlueprintOutput";
    bp_out.cpp_class = true;
    bp_out.ports["port"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    bp_out.ports["ext"] = Port{PortDirection::Out, PortType::Any, std::string("port")};
    bp_out.domains = {{Domain::Electrical}};
    bp_out.execution = make_execution(true, false, true, false, false, false, true, true, true);
    registry.types["BlueprintOutput"] = bp_out;

    TypeDefinition light;
    light.classname = "IndicatorLight";
    light.cpp_class = true;
    light.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    light.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    light.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    light.domains = {{Domain::Electrical}};
    light.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["IndicatorLight"] = light;

    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    DeviceInstance d_vin;
    d_vin.name = "vin";
    d_vin.classname = "BlueprintInput";
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    DeviceInstance d_vout;
    d_vout.name = "vout";
    d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    lamp.ports["vin"] = Port{PortDirection::In, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    registry.types["lamp_pass_through"] = lamp;

    return registry;
}

} // anonymous namespace

TEST(ProductionPathParity, CompositeAotJitTopologyParity) {
    TypeRegistry registry = build_registry_for_lamp();
    const TypeDefinition& lamp = registry.types.at("lamp_pass_through");

    auto aot_result = CodeGen::generate_composite_systems(lamp, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(lamp, registry, loading_stack);
    for (auto& dev : expanded.devices) {
        const auto* type_def = registry.get(dev.classname);
        ASSERT_NE(type_def, nullptr);
        dev = merge_device_instance(dev, *type_def);
    }

    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : expanded.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    BuildResult jit_result = build_systems_dev(expanded.devices, conn_pairs);

    auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(port);
        EXPECT_NE(it, jit_result.port_to_signal.end()) << port << " should exist in JIT map";
        return it != jit_result.port_to_signal.end() ? it->second : UINT32_MAX;
    };

    EXPECT_EQ(jit_sig("vin.port"), jit_sig("lamp.v_in"));
    EXPECT_EQ(jit_sig("lamp.v_out"), jit_sig("vout.port"));
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("vin.port"));
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("lamp.v_out"));

    EXPECT_NE(aot_result.source.find("solve_electrical"), std::string::npos);
    EXPECT_NE(aot_result.header.find("ELECTRICAL_DEBUG_MAP"), std::string::npos);
}
