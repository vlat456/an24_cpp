#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/subsolvers/subsolver_types.h"
#include "core/solvers/jit/components/port_registry.h"
#include "json_parser/json_parser.h"
#include <algorithm>

namespace {

// Helper to create a basic device instance with explicit ports
DeviceInstance make_device(const std::string& name, const std::string& classname,
                          const std::unordered_map<std::string, std::string>& params = {},
                          const std::vector<std::string>& explicit_ports = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};

    std::vector<std::string> ports;
    if (!explicit_ports.empty()) {
        ports = explicit_ports;
    } else {
        ports = get_component_ports(classname);
    }
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }
    return dev;
}

} // anonymous namespace

// ============================================================================
// Electrical Primitive Handle Tests - Batch 3
// Validates ElectricalPrimitiveHandle assignment in build_systems_dev
// ============================================================================

TEST(ElectricalHandleBuild, ElectricalSourceCreatesTheveninElement) {
    // Simple: ElectricalSource->RefNode
    // ElectricalSource should create a TheveninSource element in the electrical plan
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());

    // Verify electrical plan contains a TheveninSource element
    ASSERT_EQ(result.electrical_plan.islands.size(), 1u);
    bool found_thevenin = false;
    for (const auto& elem : result.electrical_plan.islands[0].elements) {
        if (elem.kind == ElectricalElementKind::TheveninSource) {
            found_thevenin = true;
            break;
        }
    }
    EXPECT_TRUE(found_thevenin);
}

TEST(ElectricalHandleBuild, GeneratorGetsValidHandle) {
    // Simple: Generator->RefNode
    // Generator should get a valid handle to its TheveninSource element
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.5"}, {"internal_r", "0.02"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "refnode.v"},
        {"gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());

    auto it = result.devices.find("gen");
    ASSERT_NE(it, result.devices.end());

    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&it->second);
    ASSERT_NE(gen, nullptr);
    EXPECT_TRUE(is_valid(gen->electrical_handle));
}

TEST(ElectricalHandleBuild, IndicatorLightGetsValidHandle) {
    // Simple: ElectricalSource->IndicatorLight->RefNode
    // IndicatorLight should get a valid handle to its ConductanceBranch element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("light", "IndicatorLight", {{"conductance", "2.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "light.v_in"},
        {"light.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());

    auto it = result.devices.find("light");
    ASSERT_NE(it, result.devices.end());

    const IndicatorLight<JitProvider>* light = std::get_if<IndicatorLight<JitProvider>>(&it->second);
    ASSERT_NE(light, nullptr);
    EXPECT_TRUE(is_valid(light->electrical_handle));
}

TEST(ElectricalHandleBuild, HandlePointsToCorrectElementKind) {
    // Generator->RefNode: generator handle should point to TheveninSource
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "refnode.v"},
        {"gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    const auto& island = result.electrical_plan.islands[0];
    auto it = result.devices.find("gen");
    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&it->second);
    ASSERT_LT(gen->electrical_handle.island_index, result.electrical_plan.islands.size());
    ASSERT_LT(gen->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical_plan.islands[gen->electrical_handle.island_index]
                              .elements[gen->electrical_handle.element_index];
    EXPECT_EQ(elem.kind, ElectricalElementKind::TheveninSource);
}

TEST(ElectricalHandleBuild, IndicatorHandlePointsToConductanceBranch) {
    // ElectricalSource->IndicatorLight->RefNode: light handle should point to ConductanceBranch
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("light", "IndicatorLight", {{"conductance", "2.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "light.v_in"},
        {"light.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    const auto& island = result.electrical_plan.islands[0];
    auto it = result.devices.find("light");
    const IndicatorLight<JitProvider>* light = std::get_if<IndicatorLight<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(light->electrical_handle));
    ASSERT_LT(light->electrical_handle.island_index, result.electrical_plan.islands.size());
    ASSERT_LT(light->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical_plan.islands[light->electrical_handle.island_index]
                              .elements[light->electrical_handle.element_index];
    EXPECT_EQ(elem.kind, ElectricalElementKind::ConductanceBranch);
}

TEST(ElectricalHandleBuild, DeterministicHandles) {
    // Build same device list twice and verify handles are identical
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}),
        make_device("light", "IndicatorLight", {{"conductance", "1.5"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "light.v_in"},
        {"light.v_out", "refnode.v"},
        {"gen.v_in", "refnode.v"}
    };

    auto result1 = build_systems_dev(devices, connections);
    auto result2 = build_systems_dev(devices, connections);

    // Generator handles should match
    const Generator<JitProvider>* gen1 = std::get_if<Generator<JitProvider>>(&result1.devices.at("gen"));
    const Generator<JitProvider>* gen2 = std::get_if<Generator<JitProvider>>(&result2.devices.at("gen"));
    ASSERT_NE(gen1, nullptr);
    ASSERT_NE(gen2, nullptr);
    EXPECT_EQ(gen1->electrical_handle.island_index, gen2->electrical_handle.island_index);
    EXPECT_EQ(gen1->electrical_handle.element_index, gen2->electrical_handle.element_index);

    // Light handles should match
    const IndicatorLight<JitProvider>* light1 = std::get_if<IndicatorLight<JitProvider>>(&result1.devices.at("light"));
    const IndicatorLight<JitProvider>* light2 = std::get_if<IndicatorLight<JitProvider>>(&result2.devices.at("light"));
    ASSERT_NE(light1, nullptr);
    ASSERT_NE(light2, nullptr);
    EXPECT_EQ(light1->electrical_handle.island_index, light2->electrical_handle.island_index);
    EXPECT_EQ(light1->electrical_handle.element_index, light2->electrical_handle.element_index);
}

TEST(ElectricalHandleBuild, CurrentSenseGetsValidHandle) {
    // CurrentSense should get a valid handle pointing to ConductanceBranch element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("cs", "CurrentSense"),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "cs.v_in"},
        {"cs.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    auto it = result.devices.find("cs");
    ASSERT_NE(it, result.devices.end());

    const CurrentSense<JitProvider>* cs = std::get_if<CurrentSense<JitProvider>>(&it->second);
    ASSERT_NE(cs, nullptr);
    EXPECT_TRUE(is_valid(cs->electrical_handle));
}

TEST(ElectricalHandleBuild, CurrentSenseHandlePointsToConductanceBranch) {
    // CurrentSense handle should point to ConductanceBranch element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("cs", "CurrentSense", {{"conductance", "500.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "cs.v_in"},
        {"cs.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    const auto& island = result.electrical_plan.islands[0];
    auto it = result.devices.find("cs");
    const CurrentSense<JitProvider>* cs = std::get_if<CurrentSense<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(cs->electrical_handle));
    ASSERT_LT(cs->electrical_handle.island_index, result.electrical_plan.islands.size());
    ASSERT_LT(cs->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical_plan.islands[cs->electrical_handle.island_index]
                              .elements[cs->electrical_handle.element_index];
    EXPECT_EQ(elem.kind, ElectricalElementKind::ConductanceBranch);
    EXPECT_EQ(cs->electrical_handle.component_index, elem.component_index);
}

TEST(ElectricalHandleBuild, MultipleIslandsIndependentHandles) {
    // Two independent circuits - handles should be independent
    std::vector<DeviceInstance> devices = {
        make_device("gen1", "Generator", {{"v_nominal", "12.0"}}),
        make_device("gnd1", "RefNode", {{"value", "0.0"}}),
        make_device("gen2", "Generator", {{"v_nominal", "24.0"}}),
        make_device("gnd2", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen1.v_out", "gnd1.v"},
        {"gen1.v_in", "gnd1.v"},
        {"gen2.v_out", "gnd2.v"},
        {"gen2.v_in", "gnd2.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_EQ(result.electrical_plan.islands.size(), 2u);

    const Generator<JitProvider>* gen1 = std::get_if<Generator<JitProvider>>(&result.devices.at("gen1"));
    const Generator<JitProvider>* gen2 = std::get_if<Generator<JitProvider>>(&result.devices.at("gen2"));
    ASSERT_NE(gen1, nullptr);
    ASSERT_NE(gen2, nullptr);

    // Both should have valid handles
    EXPECT_TRUE(is_valid(gen1->electrical_handle));
    EXPECT_TRUE(is_valid(gen2->electrical_handle));

    // They should be in different islands
    EXPECT_NE(gen1->electrical_handle.island_index, gen2->electrical_handle.island_index);
}

TEST(ElectricalHandleBuild, GeneratorHandlePointsToTheveninSource) {
    // Generator->RefNode: generator handle should point to TheveninSource
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {{"v_nominal", "28.5"}, {"internal_r", "0.02"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "refnode.v"},
        {"gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    const auto& island = result.electrical_plan.islands[0];
    auto it = result.devices.find("gen");
    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(gen->electrical_handle));
    ASSERT_LT(gen->electrical_handle.island_index, result.electrical_plan.islands.size());
    ASSERT_LT(gen->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical_plan.islands[gen->electrical_handle.island_index]
                              .elements[gen->electrical_handle.element_index];
    EXPECT_EQ(elem.kind, ElectricalElementKind::TheveninSource);
}

TEST(ElectricalHandleBuild, GeneratorWithParamsGetsValidHandle) {
    // Generator with parameters
    std::vector<DeviceInstance> devices = {
        make_device("gen", "Generator", {
            {"v_nominal", "28.0"}, {"internal_r", "0.01"}
        }),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "refnode.v"},
        {"gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&result.devices.at("gen"));
    ASSERT_NE(gen, nullptr);
    EXPECT_TRUE(is_valid(gen->electrical_handle));
}
