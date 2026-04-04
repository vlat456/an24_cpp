#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/aot/codegen.h"
#include "test_helpers.h"
#include <set>

namespace {

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
    lamp.classname = "voltage_indicator";
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
    registry.types["voltage_indicator"] = lamp;

    return registry;
}

} // anonymous namespace

TEST(ProductionPathParity, CompositeAotJitTopologyParity) {
    TypeRegistry registry = build_registry_for_lamp();
    const TypeDefinition& lamp = registry.types.at("voltage_indicator");

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

TEST(ProductionPathParity, MultiIslandDebugAndPlanParity) {
    TypeRegistry registry;

    TypeDefinition source_type;
    source_type.classname = "ElectricalSource";
    source_type.cpp_class = true;
    source_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    source_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    source_type.domains = {{Domain::Electrical}};
    source_type.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["ElectricalSource"] = source_type;

    TypeDefinition cond_type;
    cond_type.classname = "ElectricalConductance";
    cond_type.cpp_class = true;
    cond_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    cond_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    cond_type.domains = {{Domain::Electrical}};
    cond_type.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["ElectricalConductance"] = cond_type;

    TypeDefinition ref_type;
    ref_type.classname = "RefNode";
    ref_type.cpp_class = true;
    ref_type.ports["v"] = Port{PortDirection::In, PortType::V, std::nullopt};
    ref_type.domains = {{Domain::Electrical}};
    ref_type.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["RefNode"] = ref_type;

    TypeDefinition circuit;
    circuit.classname = "multi_island_circuit";
    circuit.cpp_class = false;

    DeviceInstance src_a;
    src_a.name = "src_a";
    src_a.classname = "ElectricalSource";
    src_a.params["voltage"] = "10.0";
    src_a.params["resistance"] = "1.0";
    src_a.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance load_a;
    load_a.name = "load_a";
    load_a.classname = "ElectricalConductance";
    load_a.params["conductance"] = "1.0";
    load_a.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance gnd_a;
    gnd_a.name = "gnd_a";
    gnd_a.classname = "RefNode";
    gnd_a.params["value"] = "0.0";
    gnd_a.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance src_b;
    src_b.name = "src_b";
    src_b.classname = "ElectricalSource";
    src_b.params["voltage"] = "24.0";
    src_b.params["resistance"] = "0.0";
    src_b.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance load_b;
    load_b.name = "load_b";
    load_b.classname = "ElectricalConductance";
    load_b.params["conductance"] = "2.0";
    load_b.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance gnd_b;
    gnd_b.name = "gnd_b";
    gnd_b.classname = "RefNode";
    gnd_b.params["value"] = "0.0";
    gnd_b.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {src_a, load_a, gnd_a, src_b, load_b, gnd_b};
    circuit.connections = {
        {"src_a.v_out", "load_a.v_in", {}},
        {"load_a.v_out", "gnd_a.v", {}},
        {"src_a.v_in", "gnd_a.v", {}},
        {"src_b.v_out", "load_b.v_in", {}},
        {"load_b.v_out", "gnd_b.v", {}},
        {"src_b.v_in", "gnd_b.v", {}}
    };
    registry.types["multi_island_circuit"] = circuit;

    auto aot_result = CodeGen::generate_composite_systems(circuit, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(circuit, registry, loading_stack);
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

    EXPECT_EQ(jit_result.electrical_plan.islands.size(), 2u)
        << "JIT must detect two electrical islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_ISLAND_COUNT = 2"), std::string::npos)
        << "AOT generated header must encode two islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_DEBUG_MAP"), std::string::npos)
        << "AOT generated header must include debug map";

    EXPECT_NE(aot_result.source.find("dump_island_debug(diag.island_index)"), std::string::npos)
        << "AOT generated source must include island dump diagnostics hook";
}
