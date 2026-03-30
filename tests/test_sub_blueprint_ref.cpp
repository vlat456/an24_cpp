#include <gtest/gtest.h>

#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include "ui/core/interned_id.h"
#include "test_execution_phases.h"

// Allow gtest to print InternedId values on assertion failure
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}


// ============================================================
// JSON Parsing: sub_blueprints array
// ============================================================

TEST(SubBlueprintParse, ParseSubBlueprintsArray) {
    std::string json_str = R"({
        "classname": "my_circuit",
        "cpp_class": false,
        "domains": ["Electrical"],
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false,
            "thermal": false
        },
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
    auto td = parse_type_definition(j);

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
        "domains": ["Electrical"],
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false,
            "thermal": false
        },
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })";

    auto j = nlohmann::json::parse(json_str);
    auto td = parse_type_definition(j);
    EXPECT_TRUE(td.sub_blueprints.empty());
}

// ============================================================
// Cycle Detection
// ============================================================

TEST(CycleDetection, DirectSelfReference_Throws) {
    TypeRegistry registry;
    TypeDefinition td;
    td.classname = "self_ref";
    td.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "me";
    ref.blueprint_path = "self_ref";
    ref.type_name = "self_ref";
    td.sub_blueprints.push_back(ref);
    registry.types["self_ref"] = td;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        expand_sub_blueprint_references(td, registry, loading_stack),
        std::runtime_error
    );
}

TEST(CycleDetection, IndirectCycle_Throws) {
    TypeRegistry registry;

    TypeDefinition td_a;
    td_a.classname = "cycle_a";
    td_a.cpp_class = false;
    SubBlueprintRef ref_b;
    ref_b.id = "b_inst";
    ref_b.blueprint_path = "cycle_b";
    ref_b.type_name = "cycle_b";
    td_a.sub_blueprints.push_back(ref_b);
    registry.types["cycle_a"] = td_a;

    TypeDefinition td_b;
    td_b.classname = "cycle_b";
    td_b.cpp_class = false;
    SubBlueprintRef ref_a;
    ref_a.id = "a_inst";
    ref_a.blueprint_path = "cycle_a";
    ref_a.type_name = "cycle_a";
    td_b.sub_blueprints.push_back(ref_a);
    registry.types["cycle_b"] = td_b;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        expand_sub_blueprint_references(td_a, registry, loading_stack),
        std::runtime_error
    );
}

// ============================================================
// Recursive Expansion
// ============================================================

TEST(SubBlueprintExpand, SingleLevel_FlattensPrefixed) {
    TypeRegistry registry;

    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    DeviceInstance d_vin;  d_vin.name = "vin";  d_vin.classname = "BlueprintInput";
    DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    DeviceInstance d_vout; d_vout.name = "vout"; d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["lamp_pass_through"] = lamp;

    TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    parent.devices = {d_bat};
    SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = expand_sub_blueprint_references(parent, registry, stack);

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
    TypeRegistry registry;

    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    d_lamp.params["color"] = "red";
    lamp.devices = {d_lamp};
    registry.types["lamp_pass_through"] = lamp;

    TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    ref.params_override["lamp.color"] = "green";
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = expand_sub_blueprint_references(parent, registry, stack);

    for (const auto& d : result.devices) {
        if (d.name == "lamp_1:lamp") {
            EXPECT_EQ(d.params.at("color"), "green");
            return;
        }
    }
    FAIL() << "lamp_1:lamp device not found in expanded result";
}

TEST(SubBlueprintExpand, TwoLevelsDeep_FullyPrefixed) {
    TypeRegistry registry;

    TypeDefinition simple_bat;
    simple_bat.classname = "simple_battery";
    simple_bat.cpp_class = false;
    DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    DeviceInstance d_vin; d_vin.name = "vin"; d_vin.classname = "BlueprintInput";
    simple_bat.devices = {d_bat, d_vin};
    registry.types["simple_battery"] = simple_bat;

    TypeDefinition bank;
    bank.classname = "battery_bank";
    bank.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "simple_battery";
    bank.sub_blueprints.push_back(ref);
    registry.types["battery_bank"] = bank;

    TypeDefinition top;
    top.classname = "top";
    top.cpp_class = false;
    SubBlueprintRef ref2;
    ref2.id = "bank_1";
    ref2.type_name = "battery_bank";
    top.sub_blueprints.push_back(ref2);
    registry.types["top"] = top;

    std::set<std::string> stack;
    auto result = expand_sub_blueprint_references(top, registry, stack);

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
        dev.execution = test_exec::electrical_passive();
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
        dev.execution = test_exec::electrical_passive();
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
        dev.execution = test_exec::electrical_passive();
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
    EXPECT_TRUE(source.find("execute") != std::string::npos);
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
        dev.execution = test_exec::electrical_passive();
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
        dev.execution = test_exec::electrical_passive();
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
        dev.execution = test_exec::electrical_passive();
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
        dev.execution = test_exec::electrical_passive();
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
// NOTE: RoundTrip_PreservesReferences, RoundTrip_BakedIn_PreservesFlag,
// and MixedMode_ReferencesAndBakedIn tests were removed as part of the
// v3 migration (they relied on FlatBlueprint serialize/deserialize).
// Equivalent coverage is provided by tests/blueprint_v2/test_codec.cpp.
