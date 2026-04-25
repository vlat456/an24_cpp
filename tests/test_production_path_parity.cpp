#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/aot/codegen.h"
#include "core/model/component_registry.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "jit_build_input_test_helper.h"
#include "ui/core/interned_id.h"
#include <set>

namespace {

ComponentRegistry build_registry_for_lamp() {
    ComponentRegistry registry;
    register_from_library(registry, {"IndicatorLight"});

    CompositeSpec lamp;
    lamp.classname = "voltage_indicator";
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.params["conductance"] = "0.002";  // Required param for ConductanceBranch solver role
    lamp.devices.push_back(d_lamp);
    lamp.bridge_ports = {
        BridgePortDefinition{"vin", "vin", bp2::BridgeDirection::Input, PortType::V},
        BridgePortDefinition{"vout", "vout", bp2::BridgeDirection::Output, PortType::V},
    };
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    lamp.ports["vin"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    registry.register_type("voltage_indicator", lamp);

    return registry;
}

} // anonymous namespace

TEST(ProductionPathParity, CompositeAotJitTopologyParity) {
    ComponentRegistry registry = build_registry_for_lamp();
    const auto& lamp_variant = registry.all_types().at("voltage_indicator");
    const CompositeSpec& lamp = std::get<CompositeSpec>(lamp_variant);

    auto aot_result = CodeGen::generate_composite_systems(lamp, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    // JIT path: Flattener path
    ui::StringInterner jit_interner;
    bp2::BlueprintLibrary jit_library;
    for (const auto& [name, spec] : registry.all_types()) {
        if (is_composite(spec)) {
            auto bp = bp2::blueprint_from_type_definition(spec, jit_interner, registry);
            jit_library.add(jit_interner.intern(name), std::move(bp));
        }
    }
    auto jit_bp = bp2::blueprint_from_type_definition(ComponentSpec{lamp}, jit_interner, registry);
    bp2::PathArena jit_arena(jit_interner);
    bp2::Flattener jit_flattener(jit_library);
    auto jit_netlist = jit_flattener.flatten(jit_bp, jit_arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(jit_netlist, jit_arena, jit_interner, registry);
    BuildResult jit_result = build_systems_dev(jit_input);

     auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(jit_result.signal_key_interner.lookup(port));
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
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "ElectricalConductance", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "multi_island_circuit";

    DeviceInstance src_a;
    src_a.name = "src_a";
    src_a.classname = "ElectricalSource";
    src_a.params["voltage"] = "10.0";
    src_a.params["resistance"] = "1.0";
    

    DeviceInstance load_a;
    load_a.name = "load_a";
    load_a.classname = "ElectricalConductance";
    load_a.params["conductance"] = "1.0";
    

    DeviceInstance gnd_a;
    gnd_a.name = "gnd_a";
    gnd_a.classname = "RefNode";
    gnd_a.params["value"] = "0.0";
    

    DeviceInstance src_b;
    src_b.name = "src_b";
    src_b.classname = "ElectricalSource";
    src_b.params["voltage"] = "24.0";
    src_b.params["resistance"] = "0.0";
    

    DeviceInstance load_b;
    load_b.name = "load_b";
    load_b.classname = "ElectricalConductance";
    load_b.params["conductance"] = "2.0";
    

    DeviceInstance gnd_b;
    gnd_b.name = "gnd_b";
    gnd_b.classname = "RefNode";
    gnd_b.params["value"] = "0.0";
    

    circuit.devices.push_back(src_a);
    circuit.devices.push_back(load_a);
    circuit.devices.push_back(gnd_a);
    circuit.devices.push_back(src_b);
    circuit.devices.push_back(load_b);
    circuit.devices.push_back(gnd_b);
    circuit.connections = {
        {"src_a.v_out", "load_a.v_in", {}},
        {"load_a.v_out", "gnd_a.v", {}},
        {"src_a.v_in", "gnd_a.v", {}},
        {"src_b.v_out", "load_b.v_in", {}},
        {"load_b.v_out", "gnd_b.v", {}},
        {"src_b.v_in", "gnd_b.v", {}}
    };
    registry.register_type("multi_island_circuit", circuit);

    auto aot_result = CodeGen::generate_composite_systems(circuit, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    // JIT path: Flattener path
    ui::StringInterner jit_interner2;
    bp2::BlueprintLibrary jit_library2;
    for (const auto& [name, spec] : registry.all_types()) {
        if (is_composite(spec)) {
            auto bp = bp2::blueprint_from_type_definition(spec, jit_interner2, registry);
            jit_library2.add(jit_interner2.intern(name), std::move(bp));
        }
    }
    auto jit_bp2 = bp2::blueprint_from_type_definition(ComponentSpec{circuit}, jit_interner2, registry);
    bp2::PathArena jit_arena2(jit_interner2);
    bp2::Flattener jit_flattener2(jit_library2);
    auto jit_netlist2 = jit_flattener2.flatten(jit_bp2, jit_arena2);
    auto jit_input2 = bp2::elaboration::elaborate_for_jit(jit_netlist2, jit_arena2, jit_interner2, registry);
    BuildResult jit_result = build_systems_dev(jit_input2);

     EXPECT_EQ(jit_result.electrical_plan.islands.size(), 2u)
        << "JIT must detect two electrical islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_ISLAND_COUNT = 2"), std::string::npos)
        << "AOT generated header must encode two islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_DEBUG_MAP"), std::string::npos)
        << "AOT generated header must include debug map";

    EXPECT_NE(aot_result.source.find("dump_island_debug(diag.island_index)"), std::string::npos)
        << "AOT generated source must include island dump diagnostics hook";
}
