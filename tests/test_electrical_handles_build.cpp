#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/subsolvers/subsolver_types.h"
#include "core/solvers/common/port_registry.h"
#include "core/registry/component_resolution.h"
#include "jit_build_input_test_helper.h"
#include <algorithm>

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "battery.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    ASSERT_FALSE(result.electrical.plan.islands.empty());

    // Verify electrical plan contains a TheveninSource element
    ASSERT_EQ(result.electrical.plan.islands.size(), 1u);
    bool found_thevenin = false;
    for (const auto& elem : result.electrical.plan.islands[0].elements) {
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    ASSERT_FALSE(result.electrical.plan.islands.empty());

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "light.v_in"},
        {"light.v_out", "refnode.v", "battery.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    ASSERT_FALSE(result.electrical.plan.islands.empty());

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const auto& island = result.electrical.plan.islands[0];
    auto it = result.devices.find("gen");
    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&it->second);
    ASSERT_LT(gen->electrical_handle.island_index, result.electrical.plan.islands.size());
    ASSERT_LT(gen->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical.plan.islands[gen->electrical_handle.island_index]
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "light.v_in"},
        {"light.v_out", "refnode.v", "battery.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const auto& island = result.electrical.plan.islands[0];
    auto it = result.devices.find("light");
    const IndicatorLight<JitProvider>* light = std::get_if<IndicatorLight<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(light->electrical_handle));
    ASSERT_LT(light->electrical_handle.island_index, result.electrical.plan.islands.size());
    ASSERT_LT(light->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical.plan.islands[light->electrical_handle.island_index]
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "light.v_in"},
        {"light.v_out", "refnode.v", "gen.v_in"}
    };

    auto result1 = build_systems_dev(make_jit_input(devices, signal_groups));
    auto result2 = build_systems_dev(make_jit_input(devices, signal_groups));

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "cs.v_in"},
        {"cs.v_out", "refnode.v", "battery.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"battery.v_out", "cs.v_in"},
        {"cs.v_out", "refnode.v", "battery.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const auto& island = result.electrical.plan.islands[0];
    auto it = result.devices.find("cs");
    const CurrentSense<JitProvider>* cs = std::get_if<CurrentSense<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(cs->electrical_handle));
    ASSERT_LT(cs->electrical_handle.island_index, result.electrical.plan.islands.size());
    ASSERT_LT(cs->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical.plan.islands[cs->electrical_handle.island_index]
                              .elements[cs->electrical_handle.element_index];
    EXPECT_EQ(elem.kind, ElectricalElementKind::ConductanceBranch);
    EXPECT_EQ(cs->electrical_handle.element_id, elem.element_id);
}

TEST(ElectricalHandleBuild, MultipleIslandsIndependentHandles) {
    // Two independent circuits - handles should be independent
    std::vector<DeviceInstance> devices = {
        make_device("gen1", "Generator", {{"v_nominal", "12.0"}}),
        make_device("gnd1", "RefNode", {{"value", "0.0"}}),
        make_device("gen2", "Generator", {{"v_nominal", "24.0"}}),
        make_device("gnd2", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen1.v_out", "gen1.v_in", "gnd1.v"},
        {"gen2.v_out", "gen2.v_in", "gnd2.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    ASSERT_EQ(result.electrical.plan.islands.size(), 2u);

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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const auto& island = result.electrical.plan.islands[0];
    auto it = result.devices.find("gen");
    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&it->second);

    ASSERT_TRUE(is_valid(gen->electrical_handle));
    ASSERT_LT(gen->electrical_handle.island_index, result.electrical.plan.islands.size());
    ASSERT_LT(gen->electrical_handle.element_index, island.elements.size());

    const auto& elem = result.electrical.plan.islands[gen->electrical_handle.island_index]
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"gen.v_out", "gen.v_in", "refnode.v"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const Generator<JitProvider>* gen = std::get_if<Generator<JitProvider>>(&result.devices.at("gen"));
    ASSERT_NE(gen, nullptr);
    EXPECT_TRUE(is_valid(gen->electrical_handle));
}

// ============================================================================
// Regression: KnobSwitch via solver_role metadata gets valid handles
// ============================================================================
// Verifies that when a KnobSwitch device uses the metadata-driven path
// (solver_role.kind == "KnobSwitchBranches"), the bind_handle flag in
// solver_role.values causes ElectricalPrimitiveHandle assignment.
// Without bind_handle, elements are created with empty device_name and
// handles are never assigned — breaking dynamic IndexSwitch patch ops.

TEST(ElectricalHandleBuild, KnobSwitchMetadataGetsHandles) {
    // Build a 3-position KnobSwitch using spec from registry
    DeviceInstance knob = make_device_with_ports("knob", "KnobSwitch", {
        {"positions", "3"}, {"initial_position", "0"},
        {"g_open", "1e-6"}, {"g_closed", "1000.0"}
    }, {"wiper", "throw1", "throw2", "throw3", "throw4", "throw5", "control", "position"});

    DeviceInstance bat = make_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}});
    DeviceInstance gnd = make_device("gnd", "RefNode", {{"value", "0.0"}});

    std::vector<DeviceInstance> devices = {bat, knob, gnd};
    std::vector<std::vector<std::string>> signal_groups = {
        {"bat.v_out", "knob.wiper"},
        {"knob.throw1", "knob.throw2", "knob.throw3", "gnd.v"},
        {"bat.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    const KnobSwitch<JitProvider>* ks = std::get_if<KnobSwitch<JitProvider>>(&result.devices.at("knob"));
    ASSERT_NE(ks, nullptr);

    // Must have exactly 3 valid handles (one per position)
    EXPECT_EQ(ks->num_handles, 3);
    for (int i = 0; i < ks->num_handles; ++i) {
        EXPECT_TRUE(is_valid(ks->electrical_handles[i]))
            << "Handle " << i << " should be valid";
    }

    // Each handle should point to a ConductanceBranch element
    for (int i = 0; i < ks->num_handles; ++i) {
        const auto& h = ks->electrical_handles[i];
        ASSERT_LT(h.island_index, result.electrical.plan.islands.size());
        const auto& island = result.electrical.plan.islands[h.island_index];
        ASSERT_LT(h.element_index, island.elements.size());
        EXPECT_EQ(island.elements[h.element_index].kind, ElectricalElementKind::ConductanceBranch)
            << "Handle " << i << " should point to ConductanceBranch";
    }
}

TEST(ElectricalHandleBuild, KnobSwitchMetadataWithoutBindHandleGetsNoHandles) {
    // Same as above but WITHOUT bind_handle — handles should NOT be assigned.
    // This is the regression case: if blueprints lack bind_handle, knob switches
    // silently fail to get handles, breaking runtime conductance patching.
    ResolvedDevice knob = make_resolved_device("knob", "KnobSwitch", {
        {"positions", "2"}, {"initial_position", "0"},
        {"g_open", "1e-6"}, {"g_closed", "1000.0"}
    });
    if (knob.solver_role) {
        knob.solver_role->value_map.erase("bind_handle");
    }

    ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}});
    ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});

    std::vector<ResolvedDevice> devices = {bat, knob, gnd};
    std::vector<std::vector<std::string>> signal_groups = {
        {"bat.v_out", "knob.wiper"},
        {"knob.throw1", "knob.throw2", "gnd.v"},
        {"bat.v_in"}
    };

    auto result = build_systems_dev(make_jit_input_resolved(devices, signal_groups));

    const KnobSwitch<JitProvider>* ks = std::get_if<KnobSwitch<JitProvider>>(&result.devices.at("knob"));
    ASSERT_NE(ks, nullptr);

    // Without bind_handle, num_handles should be 0 (elements have empty device_name)
    EXPECT_EQ(ks->num_handles, 0);
}
