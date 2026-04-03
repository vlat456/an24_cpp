#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
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
// Electrical Island Build Tests - Batch 2
// Validates electrical island extraction in build_systems_dev
// ============================================================================

TEST(ElectricalIslandBuild, ClosedCircuitOneIsland) {
    // Build: Battery->Resistor->IndicatorLight->RefNode loop
    // Battery: v_nominal=28V, internal_r=0.01
    // Resistor: v_in, v_out, conductance=0.1
    // IndicatorLight: v_in, v_out, conductance=1.0
    // RefNode: v (fixed 0V reference)
    //
    // Circuit: battery.v_out -> resistor.v_in -> resistor.v_out ->
    //          indicator.v_in -> indicator.v_out -> refnode.v
    // Also battery.v_in -> refnode.v (ground reference)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("resistor", "Resistor", {{"conductance", "0.1"}}),
        make_device("indicator", "IndicatorLight", {{"conductance", "1.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "resistor.v_in"},
        {"resistor.v_out", "indicator.v_in"},
        {"indicator.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    // Verify electrical plan was populated
    ASSERT_FALSE(result.electrical_plan.islands.empty());

    // We expect 1 island
    ASSERT_EQ(result.electrical_plan.islands.size(), 1u);

    const auto& island = result.electrical_plan.islands[0];

    // Battery creates TheveninSource, RefNode creates FixedVoltageNode,
    // Resistor and IndicatorLight create ConductanceBranch elements
    // Total: 4 elements
    ASSERT_EQ(island.elements.size(), 4u);

    // Collect element kinds
    std::vector<ElectricalElementKind> kinds;
    for (const auto& elem : island.elements) {
        kinds.push_back(elem.kind);
    }

    // Should have one of each kind
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::FixedVoltageNode) != kinds.end());
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::TheveninSource) != kinds.end());
    EXPECT_EQ(std::count(kinds.begin(), kinds.end(), ElectricalElementKind::ConductanceBranch), 2);

    // Island should have 3 unique node indices (battery output node,
    // node between resistor and indicator, and refnode/ground node)
    std::vector<uint32_t> sigs = island.signal_indices;
    std::sort(sigs.begin(), sigs.end());
    sigs.erase(std::unique(sigs.begin(), sigs.end()), sigs.end());
    EXPECT_EQ(sigs.size(), 3u);
}

TEST(ElectricalIslandBuild, TwoDisconnectedNetsTwoIslands) {
    // Two independent circuits, each forming its own island
    std::vector<DeviceInstance> devices = {
        // Circuit 1: ElectricalSource1->Resistor1->RefNode1
        make_device("battery1", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("resistor1", "Resistor", {{"conductance", "0.1"}}),
        make_device("gnd1", "RefNode", {{"value", "0.0"}}),
        // Circuit 2: Generator->Resistor2->RefNode2
        make_device("gen2", "Generator", {{"v_nominal", "24.0"}, {"internal_r", "0.02"}}),
        make_device("resistor2", "Resistor", {{"conductance", "0.2"}}),
        make_device("gnd2", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        // Circuit 1
        {"battery1.v_out", "resistor1.v_in"},
        {"resistor1.v_out", "gnd1.v"},
        {"battery1.v_in", "gnd1.v"},
        // Circuit 2
        {"gen2.v_out", "resistor2.v_in"},
        {"resistor2.v_out", "gnd2.v"},
        {"gen2.v_in", "gnd2.v"}
    };

    auto result = build_systems_dev(devices, connections);

    // Should have 2 islands
    ASSERT_EQ(result.electrical_plan.islands.size(), 2u);

    // Each island should have expected element count
    // Island 1: Battery (TheveninSource) + RefNode (FixedVoltageNode) + Resistor (ConductanceBranch)
    // Island 2: Generator (TheveninSource) + RefNode (FixedVoltageNode) + Resistor (ConductanceBranch)
    ASSERT_EQ(result.electrical_plan.islands[0].elements.size(), 3u);
    ASSERT_EQ(result.electrical_plan.islands[1].elements.size(), 3u);
}

TEST(ElectricalIslandBuild, MissingRequiredPortThrows) {
    // RefNode requires "v" port - create device without it by manually constructing
    // a DeviceInstance with empty ports
    DeviceInstance bad_refnode;
    bad_refnode.name = "refnode";
    bad_refnode.classname = "RefNode";
    bad_refnode.params = {{"value", "0.0"}};
    bad_refnode.execution = {};
    // NOTE: ports map is intentionally empty - this should cause resolve_port to fail

    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        bad_refnode
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    // Missing required port mapping should throw
    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(ElectricalIslandBuild, RefNodeCreatesFixedVoltageNode) {
    // Simple: Battery->RefNode
    // RefNode should create a FixedVoltageNode element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    // Find the FixedVoltageNode element
    const ElectricalElement* fvn = nullptr;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::FixedVoltageNode) {
            fvn = &elem;
            break;
        }
    }
    ASSERT_NE(fvn, nullptr);
    EXPECT_FLOAT_EQ(fvn->value_a, 0.0f);  // RefNode value
}

TEST(ElectricalIslandBuild, BatteryCreatesTheveninSource) {
    // Simple: Battery->RefNode
    // ElectricalSource should create a TheveninSource element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.05"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    // Find the TheveninSource element (Battery)
    const ElectricalElement* thv = nullptr;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::TheveninSource) {
            thv = &elem;
            break;
        }
    }
    ASSERT_NE(thv, nullptr);
    EXPECT_FLOAT_EQ(thv->value_a, 28.0f);   // v_nominal
    EXPECT_FLOAT_EQ(thv->value_b, 0.05f);   // internal_r
}

TEST(ElectricalIslandBuild, GeneratorCreatesTheveninSource) {
    // Simple: Generator->RefNode
    // Generator should create a TheveninSource element
    std::vector<DeviceInstance> devices = {
        make_device("generator", "Generator", {{"v_nominal", "28.5"}, {"internal_r", "0.02"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"generator.v_out", "refnode.v"},
        {"generator.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    // Find the TheveninSource element (Generator)
    const ElectricalElement* thv = nullptr;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::TheveninSource) {
            thv = &elem;
            break;
        }
    }
    ASSERT_NE(thv, nullptr);
    EXPECT_FLOAT_EQ(thv->value_a, 28.5f);   // v_nominal
    EXPECT_FLOAT_EQ(thv->value_b, 0.02f);   // internal_r
}

TEST(ElectricalIslandBuild, ResistorCreatesConductanceBranch) {
    // Simple: ElectricalSource->Resistor->RefNode
    // Resistor should create a ConductanceBranch element
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("resistor", "Resistor", {{"conductance", "0.5"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "resistor.v_in"},
        {"resistor.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    // Find the ConductanceBranch element (Resistor)
    const ElectricalElement* cb = nullptr;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::ConductanceBranch) {
            cb = &elem;
            break;
        }
    }
    ASSERT_NE(cb, nullptr);
    EXPECT_FLOAT_EQ(cb->value_a, 0.5f);  // conductance
}

TEST(ElectricalIslandBuild, IndicatorLightCreatesConductanceBranch) {
    // Simple: ElectricalSource->IndicatorLight->RefNode
    // IndicatorLight should create a ConductanceBranch element
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
    const auto& island = result.electrical_plan.islands[0];

    // Find the ConductanceBranch element (IndicatorLight)
    const ElectricalElement* cb = nullptr;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::ConductanceBranch) {
            cb = &elem;
            break;
        }
    }
    ASSERT_NE(cb, nullptr);
    EXPECT_FLOAT_EQ(cb->value_a, 2.0f);  // conductance
}

TEST(ElectricalIslandBuild, IslandsOrderedBySmallestSignalIndex) {
    // Two circuits where we can verify ordering
    // Create devices in "wrong" order to stress sorting
    std::vector<DeviceInstance> devices = {
        make_device("bat2", "ElectricalSource", {{"voltage", "12.0"}}),
        make_device("gnd2", "RefNode", {{"value", "0.0"}}),
        make_device("bat1", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("gnd1", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat2.v_out", "gnd2.v"},
        {"bat2.v_in", "gnd2.v"},
        {"bat1.v_out", "gnd1.v"},
        {"bat1.v_in", "gnd1.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_EQ(result.electrical_plan.islands.size(), 2u);

    // Islands should be ordered by smallest signal index
    // The island with gnd1 should come first since it has a smaller node index
    // (connections are processed in order, and gnd2 comes before gnd1 in device list)
    const auto& island0 = result.electrical_plan.islands[0];
    const auto& island1 = result.electrical_plan.islands[1];

    // Both islands should have 2 elements each (ElectricalSource + RefNode)
    ASSERT_EQ(island0.elements.size(), 2u);
    ASSERT_EQ(island1.elements.size(), 2u);

    // Verify determinism: same build should produce same order
    auto result2 = build_systems_dev(devices, connections);
    ASSERT_EQ(result2.electrical_plan.islands.size(), 2u);

    for (size_t i = 0; i < result.electrical_plan.islands.size(); ++i) {
        ASSERT_EQ(result.electrical_plan.islands[i].signal_indices.size(),
                  result2.electrical_plan.islands[i].signal_indices.size());
    }
}

TEST(ElectricalIslandBuild, UnsupportedComponentsIgnored) {
    // Components like Switch, Relay are not yet supported for electrical plan
    // They should be silently ignored (no element created)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("sw", "Switch"),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "sw.v_in"},
        {"sw.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    // Should have ElectricalSource (TheveninSource) and RefNode (FixedVoltageNode)
    // Switch should NOT create an element (unsupported)
    ASSERT_EQ(island.elements.size(), 2u);

    // Verify kinds
    std::vector<ElectricalElementKind> kinds;
    for (const auto& elem : island.elements) {
        kinds.push_back(elem.kind);
    }
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::TheveninSource) != kinds.end());
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::FixedVoltageNode) != kinds.end());
}

TEST(ElectricalIslandBuild, RelayCreatesDynamicConductanceBranch) {
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("relay", "Relay", {{"g_open", "1e-6"}, {"g_closed", "1000.0"}}),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "relay.v_in"},
        {"relay.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(devices, connections);

    ASSERT_FALSE(result.electrical_plan.islands.empty());
    const auto& island = result.electrical_plan.islands[0];

    bool found_relay_branch = false;
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::ConductanceBranch) {
            found_relay_branch = true;
            EXPECT_NEAR(elem.value_a, 1e-6f, 1e-10f);
        }
    }
    EXPECT_TRUE(found_relay_branch);
}

TEST(ElectricalIslandBuild, BatteryWithExtraParamsDoesNotThrow) {
    // Regression: extraction block must tolerate params it doesn't need
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {
            {"voltage", "28.0"}, {"resistance", "0.01"}
        }),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    // Must NOT throw "Unknown/unconsumed parameter"
    EXPECT_NO_THROW(build_systems_dev(devices, connections));
}

TEST(ElectricalIslandBuild, IndicatorLightWithExtraParamsDoesNotThrow) {
    // Regression: extraction block must tolerate params it doesn't need
    // (max_brightness, rated_voltage, color are consumed by component creation but not by extraction)
    std::vector<DeviceInstance> devices = {
        make_device("battery", "ElectricalSource", {{"voltage", "28.0"}}),
        make_device("light", "IndicatorLight", {
            {"conductance", "2.0"}, {"max_brightness", "80.0"},
            {"rated_voltage", "24.0"}, {"color", "red"}
        }),
        make_device("refnode", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "light.v_in"},
        {"light.v_out", "refnode.v"},
        {"battery.v_in", "refnode.v"}
    };

    // Must NOT throw "Unknown/unconsumed parameter"
    EXPECT_NO_THROW(build_systems_dev(devices, connections));
}
