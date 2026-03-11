#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "json_parser/json_parser.h"
#include "visual/scene/persist.h"
#include <nlohmann/json.hpp>

using namespace an24;

// ============================================================
// JSON Parsing: sub_blueprints array
// ============================================================

TEST(SubBlueprintParse, ParseSubBlueprintsArray) {
    std::string json_str = R"({
        "classname": "my_circuit",
        "cpp_class": false,
        "domains": ["Electrical"],
        "sub_blueprints": [
            {
                "id": "lamp_1",
                "blueprint_path": "library/systems/lamp_pass_through.json",
                "type_name": "lamp_pass_through",
                "pos": {"x": 400, "y": 300},
                "size": {"x": 120, "y": 80},
                "params_override": {
                    "lamp.color": "green"
                }
            }
        ],
        "devices": [
            {"name": "bat", "classname": "Battery"}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "lamp_1.vin"}
        ]
    })";

    auto j = nlohmann::json::parse(json_str);
    auto td = an24::parse_type_definition(j);

    ASSERT_EQ(td.sub_blueprints.size(), 1u);
    EXPECT_EQ(td.sub_blueprints[0].id, "lamp_1");
    EXPECT_EQ(td.sub_blueprints[0].blueprint_path, "library/systems/lamp_pass_through.json");
    EXPECT_EQ(td.sub_blueprints[0].type_name, "lamp_pass_through");
    EXPECT_EQ(td.sub_blueprints[0].params_override.at("lamp.color"), "green");

    EXPECT_EQ(td.devices.size(), 1u);
    EXPECT_EQ(td.connections.size(), 1u);
}

TEST(SubBlueprintParse, NoSubBlueprintsField_EmptyVector) {
    std::string json_str = R"({
        "classname": "Battery",
        "cpp_class": true,
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })";

    auto j = nlohmann::json::parse(json_str);
    auto td = an24::parse_type_definition(j);
    EXPECT_TRUE(td.sub_blueprints.empty());
}

// ============================================================
// Cycle Detection
// ============================================================

TEST(CycleDetection, DirectSelfReference_Throws) {
    an24::TypeRegistry registry;
    an24::TypeDefinition td;
    td.classname = "self_ref";
    td.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "me";
    ref.blueprint_path = "self_ref";
    ref.type_name = "self_ref";
    td.sub_blueprints.push_back(ref);
    registry.types["self_ref"] = td;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        an24::expand_sub_blueprint_references(td, registry, loading_stack),
        std::runtime_error
    );
}

TEST(CycleDetection, IndirectCycle_Throws) {
    an24::TypeRegistry registry;

    an24::TypeDefinition td_a;
    td_a.classname = "cycle_a";
    td_a.cpp_class = false;
    an24::SubBlueprintRef ref_b;
    ref_b.id = "b_inst";
    ref_b.blueprint_path = "cycle_b";
    ref_b.type_name = "cycle_b";
    td_a.sub_blueprints.push_back(ref_b);
    registry.types["cycle_a"] = td_a;

    an24::TypeDefinition td_b;
    td_b.classname = "cycle_b";
    td_b.cpp_class = false;
    an24::SubBlueprintRef ref_a;
    ref_a.id = "a_inst";
    ref_a.blueprint_path = "cycle_a";
    ref_a.type_name = "cycle_a";
    td_b.sub_blueprints.push_back(ref_a);
    registry.types["cycle_b"] = td_b;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        an24::expand_sub_blueprint_references(td_a, registry, loading_stack),
        std::runtime_error
    );
}

// ============================================================
// Recursive Expansion
// ============================================================

TEST(SubBlueprintExpand, SingleLevel_FlattensPrefixed) {
    an24::TypeRegistry registry;

    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_vin;  d_vin.name = "vin";  d_vin.classname = "BlueprintInput";
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    an24::DeviceInstance d_vout; d_vout.name = "vout"; d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["lamp_pass_through"] = lamp;

    an24::TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    an24::DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    parent.devices = {d_bat};
    an24::SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(parent, registry, stack);

    EXPECT_EQ(result.devices.size(), 4u);

    bool found_bat = false, found_vin = false, found_lamp = false, found_vout = false;
    for (const auto& d : result.devices) {
        if (d.name == "bat") found_bat = true;
        if (d.name == "lamp_1:vin") found_vin = true;
        if (d.name == "lamp_1:lamp") found_lamp = true;
        if (d.name == "lamp_1:vout") found_vout = true;
    }
    EXPECT_TRUE(found_bat);
    EXPECT_TRUE(found_vin);
    EXPECT_TRUE(found_lamp);
    EXPECT_TRUE(found_vout);

    bool found_internal_conn = false;
    for (const auto& c : result.connections) {
        if (c.from == "lamp_1:vin.port" && c.to == "lamp_1:lamp.v_in")
            found_internal_conn = true;
    }
    EXPECT_TRUE(found_internal_conn);
}

TEST(SubBlueprintExpand, OverrideParams_Applied) {
    an24::TypeRegistry registry;

    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    d_lamp.params["color"] = "red";
    lamp.devices = {d_lamp};
    registry.types["lamp_pass_through"] = lamp;

    an24::TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    ref.params_override["lamp.color"] = "green";
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(parent, registry, stack);

    for (const auto& d : result.devices) {
        if (d.name == "lamp_1:lamp") {
            EXPECT_EQ(d.params.at("color"), "green");
            return;
        }
    }
    FAIL() << "lamp_1:lamp device not found in expanded result";
}

TEST(SubBlueprintExpand, TwoLevelsDeep_FullyPrefixed) {
    an24::TypeRegistry registry;

    an24::TypeDefinition simple_bat;
    simple_bat.classname = "simple_battery";
    simple_bat.cpp_class = false;
    an24::DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    an24::DeviceInstance d_vin; d_vin.name = "vin"; d_vin.classname = "BlueprintInput";
    simple_bat.devices = {d_bat, d_vin};
    registry.types["simple_battery"] = simple_bat;

    an24::TypeDefinition bank;
    bank.classname = "battery_bank";
    bank.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "simple_battery";
    bank.sub_blueprints.push_back(ref);
    registry.types["battery_bank"] = bank;

    an24::TypeDefinition top;
    top.classname = "top";
    top.cpp_class = false;
    an24::SubBlueprintRef ref2;
    ref2.id = "bank_1";
    ref2.type_name = "battery_bank";
    top.sub_blueprints.push_back(ref2);
    registry.types["top"] = top;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(top, registry, stack);

    bool found_deep = false;
    for (const auto& d : result.devices) {
        if (d.name == "bank_1:sb_1:bat") found_deep = true;
    }
    EXPECT_TRUE(found_deep) << "Two-level deep prefix bank_1:sb_1:bat not found";
}

// ============================================================
// SubBlueprintInstance struct
// ============================================================

TEST(SubBlueprintInstance, DefaultConstruction) {
    SubBlueprintInstance sbi;
    EXPECT_TRUE(sbi.id.empty());
    EXPECT_TRUE(sbi.blueprint_path.empty());
    EXPECT_TRUE(sbi.type_name.empty());
    EXPECT_FALSE(sbi.baked_in);
    EXPECT_EQ(sbi.pos.x, 0.0f);
    EXPECT_EQ(sbi.pos.y, 0.0f);
    EXPECT_TRUE(sbi.params_override.empty());
    EXPECT_TRUE(sbi.layout_override.empty());
    EXPECT_TRUE(sbi.internal_routing.empty());
    EXPECT_TRUE(sbi.internal_node_ids.empty());
}

TEST(SubBlueprintInstance, FullConstruction) {
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.params_override["lamp.color"] = "green";
    sbi.layout_override["vin"] = {350.0f, 300.0f};
    sbi.internal_routing["vin.port->lamp.v_in"] = {{375.0f, 310.0f}};

    EXPECT_EQ(sbi.id, "lamp_1");
    EXPECT_EQ(sbi.blueprint_path, "library/systems/lamp_pass_through.json");
    EXPECT_EQ(sbi.params_override.size(), 1u);
    EXPECT_EQ(sbi.layout_override.size(), 1u);
    EXPECT_EQ(sbi.internal_routing.size(), 1u);
}

// ============================================================
// TypeDefinition now has sub_blueprints field
// ============================================================

TEST(TypeDefinition, HasSubBlueprintsField) {
    TypeDefinition td;
    EXPECT_TRUE(td.sub_blueprints.empty());

    SubBlueprintRef sbi;
    sbi.id = "bat_1";
    sbi.type_name = "simple_battery";
    td.sub_blueprints.push_back(sbi);
    EXPECT_EQ(td.sub_blueprints.size(), 1u);
}

// ============================================================
// Blueprint has sub_blueprint_instances field
// ============================================================

TEST(BlueprintSubRef, HasSubBlueprintInstancesField) {
    Blueprint bp;
    EXPECT_TRUE(bp.sub_blueprint_instances.empty());

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    bp.sub_blueprint_instances.push_back(sbi);
    EXPECT_EQ(bp.sub_blueprint_instances.size(), 1u);
}

TEST(BlueprintSubRef, FindSubBlueprintById) {
    Blueprint bp;
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    bp.sub_blueprint_instances.push_back(sbi);

    auto* found = bp.find_sub_blueprint_instance("lamp_1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type_name, "lamp_pass_through");

    EXPECT_EQ(bp.find_sub_blueprint_instance("nonexistent"), nullptr);
}

TEST(BlueprintSubRef, RemoveSubBlueprintById) {
    Blueprint bp;
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    bp.sub_blueprint_instances.push_back(sbi);

    bool removed = bp.remove_sub_blueprint_instance("lamp_1");
    EXPECT_TRUE(removed);
    EXPECT_TRUE(bp.sub_blueprint_instances.empty());

    EXPECT_FALSE(bp.remove_sub_blueprint_instance("nonexistent"));
}

// ============================================================
// Phase 7: Hierarchical AOT Codegen
// ============================================================

#include "codegen/codegen.h"

TEST(HierarchicalCodegen, SubBlueprintInstances_CodegenGeneratesCode) {
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_sig = 0;

    // RefNode (ground)
    {
        DeviceInstance dev;
        dev.name = "gnd";
        dev.classname = "RefNode";
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["gnd.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Battery
    {
        DeviceInstance dev;
        dev.name = "bat";
        dev.classname = "Battery";
        dev.params["emf"] = "28";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["bat.v_in"] = next_sig++;
        port_to_signal["bat.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Lamp (simulating sub-blueprint internal node)
    {
        DeviceInstance dev;
        dev.name = "lamp_1.lamp";
        dev.classname = "Lamp";
        dev.params["color"] = "green";
        dev.ports["vin"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["vout"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["lamp_1.lamp.vin"] = next_sig++;
        port_to_signal["lamp_1.lamp.vout"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    std::vector<Connection> connections = {
        {"bat.v_out", "lamp_1.lamp.vin"},
        {"lamp_1.lamp.vout", "gnd.v_out"}
    };

    std::string header = CodeGen::generate_header("test_hierarchical.json", devices, connections, port_to_signal, next_sig);
    std::string source = CodeGen::generate_source("generated_test_hierarchical.h", devices, connections, port_to_signal, next_sig);

    EXPECT_FALSE(header.empty());
    EXPECT_FALSE(source.empty());

    EXPECT_TRUE(header.find("test_hierarchical") != std::string::npos);
    EXPECT_TRUE(source.find("solve_electrical") != std::string::npos);
    EXPECT_TRUE(source.find("bat") != std::string::npos);
    EXPECT_TRUE(source.find("lamp_1") != std::string::npos);
}

TEST(HierarchicalCodegen, MultipleSubBlueprints_CodegenHandlesAll) {
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_sig = 1;

    // Ground
    {
        DeviceInstance dev;
        dev.name = "gnd";
        dev.classname = "RefNode";
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["gnd.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Battery
    {
        DeviceInstance dev;
        dev.name = "bat";
        dev.classname = "Battery";
        dev.params["emf"] = "28";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["bat.v_in"] = next_sig++;
        port_to_signal["bat.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Lamp 1
    {
        DeviceInstance dev;
        dev.name = "lamp_A.lamp";
        dev.classname = "Lamp";
        dev.ports["vin"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["vout"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["lamp_A.lamp.vin"] = next_sig++;
        port_to_signal["lamp_A.lamp.vout"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Lamp 2
    {
        DeviceInstance dev;
        dev.name = "lamp_B.lamp";
        dev.classname = "Lamp";
        dev.ports["vin"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["vout"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["lamp_B.lamp.vin"] = next_sig++;
        port_to_signal["lamp_B.lamp.vout"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    std::vector<Connection> connections = {
        {"bat.v_out", "lamp_A.lamp.vin"},
        {"lamp_A.lamp.vout", "lamp_B.lamp.vin"},
        {"lamp_B.lamp.vout", "gnd.v_out"}
    };

    std::string source = CodeGen::generate_source("generated_multi.h", devices, connections, port_to_signal, next_sig);

    EXPECT_TRUE(source.find("lamp_A") != std::string::npos);
    EXPECT_TRUE(source.find("lamp_B") != std::string::npos);
}

// ============================================================
// Phase 3: Persistence round-trip for sub_blueprint_instances
// ============================================================

TEST(SubBlueprintPersist, RoundTrip_PreservesReferences) {
    Blueprint bp;

    // Add a regular node
    Node bat;
    bat.id = "bat";
    bat.type_name = "Battery";
    bat.pos = {100.0f, 200.0f};
    bat.size = {120.0f, 80.0f};
    bat.output("v_out");
    bp.add_node(bat);

    // Add an expandable (collapsed) node for the sub-blueprint
    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = {400.0f, 300.0f};
    collapsed.size = {120.0f, 80.0f};
    collapsed.input("vin");
    bp.add_node(collapsed);

    // Internal nodes of the sub-blueprint
    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    bp.add_node(vin);

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    bp.add_node(lamp);

    // Sub-blueprint instance with overrides
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.baked_in = false;
    sbi.params_override["lamp.color"] = "green";
    sbi.layout_override["vin"] = {350.0f, 300.0f};
    sbi.internal_routing["vin.port->lamp.v_in"] = {{375.0f, 310.0f}};
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp"};
    bp.sub_blueprint_instances.push_back(sbi);

    // Wire between bat and collapsed node
    Wire w;
    w.id = "w1";
    w.start = WireEnd("bat", "v_out", PortSide::Output);
    w.end = WireEnd("lamp_1", "vin", PortSide::Input);
    bp.add_wire(std::move(w));

    // Round-trip: save → load
    std::string json_str = blueprint_to_editor_json(bp);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    // Verify sub_blueprint_instances survived
    ASSERT_EQ(loaded->sub_blueprint_instances.size(), 1u);
    const auto& loaded_sbi = loaded->sub_blueprint_instances[0];
    EXPECT_EQ(loaded_sbi.id, "lamp_1");
    EXPECT_EQ(loaded_sbi.blueprint_path, "library/systems/lamp_pass_through.json");
    EXPECT_EQ(loaded_sbi.type_name, "lamp_pass_through");
    EXPECT_FALSE(loaded_sbi.baked_in);

    // Verify overrides survived round-trip
    ASSERT_EQ(loaded_sbi.params_override.size(), 1u);
    EXPECT_EQ(loaded_sbi.params_override.at("lamp.color"), "green");

    // layout_override now includes both the original manual entry ("vin")
    // and snapshotted live positions for all internal nodes (lamp_1:vin, lamp_1:lamp).
    // The snapshot adds prefixed IDs from the live node positions.
    EXPECT_GE(loaded_sbi.layout_override.size(), 1u);
    // Original unprefixed entry preserved
    ASSERT_TRUE(loaded_sbi.layout_override.count("vin"))
        << "Original layout_override entry should survive round-trip";
    EXPECT_FLOAT_EQ(loaded_sbi.layout_override.at("vin").x, 350.0f);
    EXPECT_FLOAT_EQ(loaded_sbi.layout_override.at("vin").y, 300.0f);

    ASSERT_EQ(loaded_sbi.internal_routing.size(), 1u);
    const auto& rp = loaded_sbi.internal_routing.at("vin.port->lamp.v_in");
    ASSERT_EQ(rp.size(), 1u);
    EXPECT_FLOAT_EQ(rp[0].x, 375.0f);
    EXPECT_FLOAT_EQ(rp[0].y, 310.0f);

    // Verify nodes and wires after re-expansion from registry.
    // Save strips internal nodes of non-baked-in SBIs (Phase 1).
    // Load re-expands them from the TypeRegistry (Phase 2).
    // Original bp had 4 nodes (bat, lamp_1, lamp_1:vin, lamp_1:lamp).
    // After save: 2 saved (bat, lamp_1 collapsed). Internal nodes stripped.
    // After load: 2 from JSON + 3 re-expanded from registry (vin, lamp, vout) = 5.
    EXPECT_GE(loaded->nodes.size(), 2u);  // At least bat + lamp_1 (collapsed)
    // Re-expanded internal nodes should include lamp_1:vin, lamp_1:lamp, lamp_1:vout
    EXPECT_NE(loaded->find_node("lamp_1:vin"), nullptr);
    EXPECT_NE(loaded->find_node("lamp_1:lamp"), nullptr);
    EXPECT_NE(loaded->find_node("lamp_1:vout"), nullptr);
    // External wire should survive
    EXPECT_GE(loaded->wires.size(), 1u);
}

TEST(SubBlueprintPersist, RoundTrip_BakedIn_PreservesFlag) {
    Blueprint bp;

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    bp.add_node(lamp);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.baked_in = true;
    sbi.internal_node_ids = {"lamp_1:lamp"};
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json_str = blueprint_to_editor_json(bp);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->sub_blueprint_instances.size(), 1u);
    EXPECT_TRUE(loaded->sub_blueprint_instances[0].baked_in);
    EXPECT_EQ(loaded->sub_blueprint_instances[0].blueprint_path,
              "library/systems/lamp_pass_through.json");
    // Baked-in instances have no overrides
    EXPECT_TRUE(loaded->sub_blueprint_instances[0].params_override.empty());
}

TEST(SubBlueprintPersist, MixedMode_ReferencesAndBakedIn) {
    Blueprint bp;

    // Baked-in instance
    Node col1;
    col1.id = "lamp_1";
    col1.type_name = "lamp_pass_through";
    col1.expandable = true;
    bp.add_node(col1);

    Node l1;
    l1.id = "lamp_1:lamp";
    l1.type_name = "IndicatorLight";
    l1.group_id = "lamp_1";
    bp.add_node(l1);

    SubBlueprintInstance sbi1;
    sbi1.id = "lamp_1";
    sbi1.type_name = "lamp_pass_through";
    sbi1.baked_in = true;
    sbi1.internal_node_ids = {"lamp_1:lamp"};
    bp.sub_blueprint_instances.push_back(sbi1);

    // Reference instance
    Node col2;
    col2.id = "lamp_2";
    col2.type_name = "lamp_pass_through";
    col2.expandable = true;
    bp.add_node(col2);

    Node l2;
    l2.id = "lamp_2:lamp";
    l2.type_name = "IndicatorLight";
    l2.group_id = "lamp_2";
    bp.add_node(l2);

    SubBlueprintInstance sbi2;
    sbi2.id = "lamp_2";
    sbi2.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi2.type_name = "lamp_pass_through";
    sbi2.baked_in = false;
    sbi2.params_override["lamp.color"] = "blue";
    sbi2.internal_node_ids = {"lamp_2:lamp"};
    bp.sub_blueprint_instances.push_back(sbi2);

    std::string json_str = blueprint_to_editor_json(bp);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->sub_blueprint_instances.size(), 2u);

    // Find each by id
    const SubBlueprintInstance* baked = nullptr;
    const SubBlueprintInstance* ref = nullptr;
    for (const auto& s : loaded->sub_blueprint_instances) {
        if (s.id == "lamp_1") baked = &s;
        if (s.id == "lamp_2") ref = &s;
    }
    ASSERT_NE(baked, nullptr);
    ASSERT_NE(ref, nullptr);

    EXPECT_TRUE(baked->baked_in);
    EXPECT_TRUE(baked->params_override.empty());

    EXPECT_FALSE(ref->baked_in);
    ASSERT_EQ(ref->params_override.size(), 1u);
    EXPECT_EQ(ref->params_override.at("lamp.color"), "blue");
}
