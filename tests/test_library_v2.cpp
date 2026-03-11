/// Phase 2 tests: Library v1 ↔ v2 conversion
///
/// Tests that TypeDefinition can be converted to BlueprintV2 and back,
/// and that load_type_registry() works with .blueprint files.

#include <gtest/gtest.h>
#include "blueprint_v2.h"
#include "convert.h"
#include "json_parser.h"
#include <filesystem>
#include <fstream>

using namespace an24;
using namespace an24::v2;

// ==================================================================
// Test 1: ConvertCppComponent — Battery → BlueprintV2 roundtrip
// ==================================================================

TEST(LibraryV2, ConvertCppComponent) {
    // Build a Battery TypeDefinition (mirrors library/electrical/Battery.json)
    TypeDefinition td;
    td.classname = "Battery";
    td.description = "Battery voltage source with internal resistance";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.params["v_nominal"] = "28.0";
    td.params["internal_r"] = "0.01";
    td.domains = std::vector<Domain>{Domain::Electrical};
    td.priority = "high";
    td.critical = true;

    // Convert to v2
    BlueprintV2 bp = type_definition_to_v2(td);

    // Assert v2 structure
    EXPECT_EQ(bp.version, 2);
    EXPECT_EQ(bp.meta.name, "Battery");
    EXPECT_EQ(bp.meta.description, "Battery voltage source with internal resistance");
    EXPECT_TRUE(bp.meta.cpp_class);
    ASSERT_EQ(bp.meta.domains.size(), 1u);
    EXPECT_EQ(bp.meta.domains[0], "Electrical");
    EXPECT_EQ(bp.meta.priority, "high");
    EXPECT_TRUE(bp.meta.critical);

    // Exposes (ports)
    ASSERT_EQ(bp.exposes.size(), 2u);
    EXPECT_EQ(bp.exposes.at("v_in").direction, "In");
    EXPECT_EQ(bp.exposes.at("v_in").type, "V");
    EXPECT_EQ(bp.exposes.at("v_out").direction, "Out");
    EXPECT_EQ(bp.exposes.at("v_out").type, "V");

    // Params (cpp_class → ParamDef with type + default)
    ASSERT_EQ(bp.params.size(), 2u);
    EXPECT_EQ(bp.params.at("v_nominal").default_val, "28.0");
    EXPECT_EQ(bp.params.at("v_nominal").type, "float");
    EXPECT_EQ(bp.params.at("internal_r").default_val, "0.01");

    // No nodes (C++ component)
    EXPECT_TRUE(bp.nodes.empty());
    EXPECT_TRUE(bp.wires.empty());

    // Roundtrip back to TypeDefinition
    TypeDefinition td2 = v2_to_type_definition(bp);
    EXPECT_EQ(td2.classname, "Battery");
    EXPECT_TRUE(td2.cpp_class);
    EXPECT_EQ(td2.priority, "high");
    EXPECT_TRUE(td2.critical);
    EXPECT_EQ(td2.params.at("v_nominal"), "28.0");
    EXPECT_EQ(td2.params.at("internal_r"), "0.01");
    ASSERT_TRUE(td2.domains.has_value());
    ASSERT_EQ(td2.domains->size(), 1u);
    EXPECT_EQ(td2.domains->at(0), Domain::Electrical);
    EXPECT_EQ(td2.ports.at("v_in").direction, PortDirection::In);
    EXPECT_EQ(td2.ports.at("v_out").direction, PortDirection::Out);
}

// ==================================================================
// Test 2: ConvertComposite — lamp_pass_through with devices+connections
// ==================================================================

TEST(LibraryV2, ConvertComposite) {
    // Build a composite TypeDefinition (mirrors library/systems/lamp_pass_through.json)
    TypeDefinition td;
    td.classname = "lamp_pass_through";
    td.description = "Voltage pass-through with indicator lamp for debug";
    td.cpp_class = false;
    td.ports["vin"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["vout"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.domains = std::vector<Domain>{Domain::Electrical};
    td.priority = "high";
    td.critical = true;

    // Devices
    DeviceInstance vin_dev;
    vin_dev.name = "vin";
    vin_dev.classname = "BlueprintInput";
    vin_dev.params["exposed_type"] = "V";
    vin_dev.params["exposed_direction"] = "In";
    td.devices.push_back(vin_dev);

    DeviceInstance lamp_dev;
    lamp_dev.name = "lamp";
    lamp_dev.classname = "IndicatorLight";
    lamp_dev.params["max_brightness"] = "100.0";
    lamp_dev.params["color"] = "red";
    td.devices.push_back(lamp_dev);

    DeviceInstance vout_dev;
    vout_dev.name = "vout";
    vout_dev.classname = "BlueprintOutput";
    vout_dev.params["exposed_type"] = "V";
    vout_dev.params["exposed_direction"] = "Out";
    td.devices.push_back(vout_dev);

    // Connections
    td.connections.push_back(Connection{"vin.port", "lamp.v_in", {}});
    td.connections.push_back(Connection{"lamp.v_out", "vout.port", {}});

    // Convert to v2
    BlueprintV2 bp = type_definition_to_v2(td);

    // Assert v2 nodes
    ASSERT_EQ(bp.nodes.size(), 3u);
    EXPECT_EQ(bp.nodes.at("vin").type, "BlueprintInput");
    EXPECT_EQ(bp.nodes.at("lamp").type, "IndicatorLight");
    EXPECT_EQ(bp.nodes.at("vout").type, "BlueprintOutput");

    // Assert node params
    EXPECT_EQ(bp.nodes.at("lamp").params.at("max_brightness"), "100.0");
    EXPECT_EQ(bp.nodes.at("lamp").params.at("color"), "red");

    // Assert v2 wires
    ASSERT_EQ(bp.wires.size(), 2u);
    EXPECT_EQ(bp.wires[0].from.node, "vin");
    EXPECT_EQ(bp.wires[0].from.port, "port");
    EXPECT_EQ(bp.wires[0].to.node, "lamp");
    EXPECT_EQ(bp.wires[0].to.port, "v_in");
    EXPECT_EQ(bp.wires[1].from.node, "lamp");
    EXPECT_EQ(bp.wires[1].from.port, "v_out");
    EXPECT_EQ(bp.wires[1].to.node, "vout");
    EXPECT_EQ(bp.wires[1].to.port, "port");

    // Exposes
    ASSERT_EQ(bp.exposes.size(), 2u);
    EXPECT_EQ(bp.exposes.at("vin").direction, "In");
    EXPECT_EQ(bp.exposes.at("vout").direction, "Out");

    // Params should be empty for composites
    EXPECT_TRUE(bp.params.empty());

    // Roundtrip back
    TypeDefinition td2 = v2_to_type_definition(bp);
    EXPECT_EQ(td2.classname, "lamp_pass_through");
    EXPECT_FALSE(td2.cpp_class);
    ASSERT_EQ(td2.devices.size(), 3u);
    ASSERT_EQ(td2.connections.size(), 2u);
    EXPECT_EQ(td2.connections[0].from, "vin.port");
    EXPECT_EQ(td2.connections[0].to, "lamp.v_in");
}

// ==================================================================
// Test 3: PortAliasPreserved — Splitter alias survives conversion
// ==================================================================

TEST(LibraryV2, PortAliasPreserved) {
    // Build Splitter TypeDefinition (mirrors library/Splitter.json)
    TypeDefinition td;
    td.classname = "Splitter";
    td.cpp_class = true;
    td.ports["i"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    td.ports["o1"] = Port{PortDirection::Out, PortType::Any, std::string("i")};
    td.ports["o2"] = Port{PortDirection::Out, PortType::Any, std::string("i")};
    td.domains = std::vector<Domain>{Domain::Electrical, Domain::Mechanical, Domain::Hydraulic, Domain::Thermal};
    td.priority = "high";
    td.size = {3.0f, 3.0f};

    // Convert to v2
    BlueprintV2 bp = type_definition_to_v2(td);

    // Assert aliases preserved
    EXPECT_FALSE(bp.exposes.at("i").alias.has_value());
    ASSERT_TRUE(bp.exposes.at("o1").alias.has_value());
    EXPECT_EQ(*bp.exposes.at("o1").alias, "i");
    ASSERT_TRUE(bp.exposes.at("o2").alias.has_value());
    EXPECT_EQ(*bp.exposes.at("o2").alias, "i");

    // Assert size
    ASSERT_TRUE(bp.meta.size.has_value());
    EXPECT_FLOAT_EQ((*bp.meta.size)[0], 3.0f);
    EXPECT_FLOAT_EQ((*bp.meta.size)[1], 3.0f);

    // Assert multi-domain
    ASSERT_EQ(bp.meta.domains.size(), 4u);

    // Roundtrip
    TypeDefinition td2 = v2_to_type_definition(bp);
    ASSERT_TRUE(td2.ports.at("o1").alias.has_value());
    EXPECT_EQ(*td2.ports.at("o1").alias, "i");
    ASSERT_TRUE(td2.ports.at("o2").alias.has_value());
    EXPECT_EQ(*td2.ports.at("o2").alias, "i");
    EXPECT_FALSE(td2.ports.at("i").alias.has_value());
    ASSERT_TRUE(td2.size.has_value());
    EXPECT_FLOAT_EQ(td2.size->first, 3.0f);
    EXPECT_FLOAT_EQ(td2.size->second, 3.0f);
}

// ==================================================================
// Test 4: LoadRegistryFromBlueprint — load .blueprint files
// ==================================================================

TEST(LibraryV2, LoadRegistryFromBlueprint) {
    // Create temp directory with .blueprint files
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_load";
    std::filesystem::create_directories(temp_dir / "electrical");

    // Write a C++ component .blueprint
    {
        BlueprintV2 bp;
        bp.meta.name = "TestBattery";
        bp.meta.description = "Test battery";
        bp.meta.cpp_class = true;
        bp.meta.domains = {"Electrical"};
        bp.meta.priority = "high";
        bp.meta.critical = true;
        bp.exposes["v_in"] = ExposedPort{"In", "V", std::nullopt};
        bp.exposes["v_out"] = ExposedPort{"Out", "V", std::nullopt};
        bp.params["v_nominal"] = ParamDef{"float", "28.0"};

        std::ofstream f(temp_dir / "electrical" / "TestBattery.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    // Write a composite .blueprint
    {
        BlueprintV2 bp;
        bp.meta.name = "TestComposite";
        bp.meta.description = "Test composite";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};
        bp.exposes["vin"] = ExposedPort{"In", "V", std::nullopt};
        NodeV2 node;
        node.type = "TestBattery";
        bp.nodes["bat"] = node;

        std::ofstream f(temp_dir / "TestComposite.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    // Load registry
    TypeRegistry registry = load_type_registry(temp_dir.string());

    // Assert types loaded
    EXPECT_TRUE(registry.has("TestBattery"));
    EXPECT_TRUE(registry.has("TestComposite"));

    // Assert Battery fields
    auto* bat_def = registry.get("TestBattery");
    ASSERT_NE(bat_def, nullptr);
    EXPECT_TRUE(bat_def->cpp_class);
    EXPECT_EQ(bat_def->priority, "high");
    EXPECT_TRUE(bat_def->critical);
    EXPECT_EQ(bat_def->params.at("v_nominal"), "28.0");

    // Assert Composite fields
    auto* comp_def = registry.get("TestComposite");
    ASSERT_NE(comp_def, nullptr);
    EXPECT_FALSE(comp_def->cpp_class);
    ASSERT_EQ(comp_def->devices.size(), 1u);
    EXPECT_EQ(comp_def->devices[0].classname, "TestBattery");

    // Cleanup
    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 5: LoadRegistryPreservesCategories
// ==================================================================

TEST(LibraryV2, LoadRegistryPreservesCategories) {
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_categories";
    std::filesystem::create_directories(temp_dir / "electrical");
    std::filesystem::create_directories(temp_dir / "systems");

    // Write electrical component
    {
        BlueprintV2 bp;
        bp.meta.name = "CatBattery";
        bp.meta.cpp_class = true;
        bp.meta.domains = {"Electrical"};
        bp.exposes["v_out"] = ExposedPort{"Out", "V", std::nullopt};

        std::ofstream f(temp_dir / "electrical" / "CatBattery.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    // Write systems composite
    {
        BlueprintV2 bp;
        bp.meta.name = "CatSystem";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};
        NodeV2 node;
        node.type = "CatBattery";
        bp.nodes["bat"] = node;

        std::ofstream f(temp_dir / "systems" / "CatSystem.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    TypeRegistry registry = load_type_registry(temp_dir.string());

    EXPECT_EQ(registry.categories.at("CatBattery"), "electrical");
    EXPECT_EQ(registry.categories.at("CatSystem"), "systems");

    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 6: LoadRegistryCompositesHaveDevices
// ==================================================================

TEST(LibraryV2, LoadRegistryCompositesHaveDevices) {
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_composites";
    std::filesystem::create_directories(temp_dir);

    // Write a composite with nodes and wires
    {
        BlueprintV2 bp;
        bp.meta.name = "CompWithDevices";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};

        NodeV2 input_node;
        input_node.type = "BlueprintInput";
        input_node.params["exposed_type"] = "V";
        bp.nodes["vin"] = input_node;

        NodeV2 output_node;
        output_node.type = "BlueprintOutput";
        output_node.params["exposed_type"] = "V";
        bp.nodes["vout"] = output_node;

        WireV2 wire;
        wire.id = "w0";
        wire.from = {"vin", "port"};
        wire.to = {"vout", "port"};
        bp.wires.push_back(wire);

        std::ofstream f(temp_dir / "CompWithDevices.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    TypeRegistry registry = load_type_registry(temp_dir.string());
    auto* def = registry.get("CompWithDevices");
    ASSERT_NE(def, nullptr);
    EXPECT_FALSE(def->cpp_class);

    // Back-converted composites should have devices and connections
    ASSERT_EQ(def->devices.size(), 2u);
    ASSERT_EQ(def->connections.size(), 1u);
    EXPECT_EQ(def->connections[0].from, "vin.port");
    EXPECT_EQ(def->connections[0].to, "vout.port");

    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 7: RoundtripRegistryThroughV2
// ==================================================================

TEST(LibraryV2, RoundtripRegistryThroughV2) {
    // Build a mini registry with both a C++ component and a composite
    TypeDefinition bat;
    bat.classname = "RoundBattery";
    bat.description = "Test battery";
    bat.cpp_class = true;
    bat.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    bat.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    bat.params["v_nominal"] = "28.0";
    bat.domains = std::vector<Domain>{Domain::Electrical};
    bat.priority = "high";
    bat.critical = true;

    TypeDefinition comp;
    comp.classname = "RoundComposite";
    comp.description = "Test composite";
    comp.cpp_class = false;
    comp.ports["vin"] = Port{PortDirection::In, PortType::V, std::nullopt};
    comp.domains = std::vector<Domain>{Domain::Electrical};

    DeviceInstance dev;
    dev.name = "bat";
    dev.classname = "RoundBattery";
    dev.params["v_nominal"] = "24.0";
    comp.devices.push_back(dev);

    comp.connections.push_back(Connection{"bat.v_in", "bat.v_out", {}});

    // Write v2 .blueprint files to temp dir
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_roundtrip";
    std::filesystem::create_directories(temp_dir / "electrical");

    {
        BlueprintV2 bp = type_definition_to_v2(bat);
        std::ofstream f(temp_dir / "electrical" / "RoundBattery.blueprint");
        f << serialize_blueprint_v2(bp);
    }
    {
        BlueprintV2 bp = type_definition_to_v2(comp);
        std::ofstream f(temp_dir / "RoundComposite.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    // Load registry from v2 files
    TypeRegistry registry = load_type_registry(temp_dir.string());

    // Verify roundtrip
    auto* bat2 = registry.get("RoundBattery");
    ASSERT_NE(bat2, nullptr);
    EXPECT_EQ(bat2->classname, "RoundBattery");
    EXPECT_TRUE(bat2->cpp_class);
    EXPECT_EQ(bat2->priority, "high");
    EXPECT_TRUE(bat2->critical);
    EXPECT_EQ(bat2->params.at("v_nominal"), "28.0");

    auto* comp2 = registry.get("RoundComposite");
    ASSERT_NE(comp2, nullptr);
    EXPECT_FALSE(comp2->cpp_class);
    ASSERT_EQ(comp2->devices.size(), 1u);
    EXPECT_EQ(comp2->devices[0].classname, "RoundBattery");
    EXPECT_EQ(comp2->devices[0].params.at("v_nominal"), "24.0");
    ASSERT_EQ(comp2->connections.size(), 1u);
    EXPECT_EQ(comp2->connections[0].from, "bat.v_in");

    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 8: BlueprintExtensionOnly — .json files ignored
// ==================================================================

TEST(LibraryV2, BlueprintExtensionOnly) {
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_extension";
    std::filesystem::create_directories(temp_dir);

    // Write a .json file (should be ignored)
    {
        std::ofstream f(temp_dir / "OldFormat.json");
        f << R"({"classname":"OldFormat","cpp_class":true,"ports":{},"params":{}})";
    }

    // Write a .blueprint file (should be loaded)
    {
        BlueprintV2 bp;
        bp.meta.name = "NewFormat";
        bp.meta.cpp_class = true;

        std::ofstream f(temp_dir / "NewFormat.blueprint");
        f << serialize_blueprint_v2(bp);
    }

    TypeRegistry registry = load_type_registry(temp_dir.string());

    EXPECT_FALSE(registry.has("OldFormat"));
    EXPECT_TRUE(registry.has("NewFormat"));

    std::filesystem::remove_all(temp_dir);
}
