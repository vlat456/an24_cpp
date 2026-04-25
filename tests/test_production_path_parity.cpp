#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/aot/codegen.h"
#include "core/model/component_registry.h"
#include "jit_build_input_test_helper.h"

TEST(ProductionPathParity, CompositeAotJitTopologyParity) {
    ComponentRegistry registry = build_voltage_indicator_registry();
    const auto& lamp_variant = registry.all_types().at("voltage_indicator");
    const CompositeSpec& lamp = std::get<CompositeSpec>(lamp_variant);

    auto aot_result = CodeGen::generate_composite_systems(lamp, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    BuildResult jit_result = run_jit_flattener_path(lamp, registry);

    EXPECT_EQ(jit_signal_of(jit_result, "vin.port"), jit_signal_of(jit_result, "lamp.v_in"));
    EXPECT_EQ(jit_signal_of(jit_result, "lamp.v_out"), jit_signal_of(jit_result, "vout.port"));
    EXPECT_NE(jit_signal_of(jit_result, "lamp.brightness"), jit_signal_of(jit_result, "vin.port"));
    EXPECT_NE(jit_signal_of(jit_result, "lamp.brightness"), jit_signal_of(jit_result, "lamp.v_out"));

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

    BuildResult jit_result = run_jit_flattener_path(circuit, registry);

    EXPECT_EQ(jit_result.electrical_plan.islands.size(), 2u)
        << "JIT must detect two electrical islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_ISLAND_COUNT = 2"), std::string::npos)
        << "AOT generated header must encode two islands";

    EXPECT_NE(aot_result.header.find("ELECTRICAL_DEBUG_MAP"), std::string::npos)
        << "AOT generated header must include debug map";

    EXPECT_NE(aot_result.source.find("dump_island_debug(diag.island_index)"), std::string::npos)
        << "AOT generated source must include island dump diagnostics hook";
}
