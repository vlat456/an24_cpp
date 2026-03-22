/// Phase 2 tests: Library v1 ↔ Flat conversion
///
/// Tests that TypeDefinition can be converted to FlatBlueprint and back,
/// and that load_type_registry() works with .blueprint files.

#include <gtest/gtest.h>
#include "editor/data/flat_blueprint.h"
#include "editor/data/type_def_convert.h"
#include "editor/data/blueprint.h"
#include "json_parser.h"
#include <filesystem>
#include <fstream>

// ==================================================================
// Test 1: ConvertCppComponent — Battery → FlatBlueprint roundtrip
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
    FlatBlueprint bp = type_definition_to_flat(td);

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
    TypeDefinition td2 = flat_to_type_definition(bp);
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
    FlatBlueprint bp = type_definition_to_flat(td);

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
    TypeDefinition td2 = flat_to_type_definition(bp);
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
    FlatBlueprint bp = type_definition_to_flat(td);

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
    TypeDefinition td2 = flat_to_type_definition(bp);
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
        FlatBlueprint bp;
        bp.meta.name = "TestBattery";
        bp.meta.description = "Test battery";
        bp.meta.cpp_class = true;
        bp.meta.domains = {"Electrical"};
        bp.meta.priority = "high";
        bp.meta.critical = true;
        bp.exposes["v_in"] = FlatPort{"In", "V", std::nullopt};
        bp.exposes["v_out"] = FlatPort{"Out", "V", std::nullopt};
        bp.params["v_nominal"] = FlatParam{"float", "28.0"};

        std::ofstream f(temp_dir / "electrical" / "TestBattery.blueprint");
        f << serialize_flat_blueprint(bp);
    }

    // Write a composite .blueprint
    {
        FlatBlueprint bp;
        bp.meta.name = "TestComposite";
        bp.meta.description = "Test composite";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};
        bp.exposes["vin"] = FlatPort{"In", "V", std::nullopt};
        FlatNode node;
        node.type = "TestBattery";
        bp.nodes["bat"] = node;

        std::ofstream f(temp_dir / "TestComposite.blueprint");
        f << serialize_flat_blueprint(bp);
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
        FlatBlueprint bp;
        bp.meta.name = "CatBattery";
        bp.meta.cpp_class = true;
        bp.meta.domains = {"Electrical"};
        bp.exposes["v_out"] = FlatPort{"Out", "V", std::nullopt};

        std::ofstream f(temp_dir / "electrical" / "CatBattery.blueprint");
        f << serialize_flat_blueprint(bp);
    }

    // Write systems composite
    {
        FlatBlueprint bp;
        bp.meta.name = "CatSystem";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};
        FlatNode node;
        node.type = "CatBattery";
        bp.nodes["bat"] = node;

        std::ofstream f(temp_dir / "systems" / "CatSystem.blueprint");
        f << serialize_flat_blueprint(bp);
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
        FlatBlueprint bp;
        bp.meta.name = "CompWithDevices";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};

        FlatNode input_node;
        input_node.type = "BlueprintInput";
        input_node.params["exposed_type"] = "V";
        bp.nodes["vin"] = input_node;

        FlatNode output_node;
        output_node.type = "BlueprintOutput";
        output_node.params["exposed_type"] = "V";
        bp.nodes["vout"] = output_node;

        FlatWire wire;
        wire.id = "w0";
        wire.from = {"vin", "port"};
        wire.to = {"vout", "port"};
        bp.wires.push_back(wire);

        std::ofstream f(temp_dir / "CompWithDevices.blueprint");
        f << serialize_flat_blueprint(bp);
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
        FlatBlueprint bp = type_definition_to_flat(bat);
        std::ofstream f(temp_dir / "electrical" / "RoundBattery.blueprint");
        f << serialize_flat_blueprint(bp);
    }
    {
        FlatBlueprint bp = type_definition_to_flat(comp);
        std::ofstream f(temp_dir / "RoundComposite.blueprint");
        f << serialize_flat_blueprint(bp);
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
        FlatBlueprint bp;
        bp.meta.name = "NewFormat";
        bp.meta.cpp_class = true;

        std::ofstream f(temp_dir / "NewFormat.blueprint");
        f << serialize_flat_blueprint(bp);
    }

    TypeRegistry registry = load_type_registry(temp_dir.string());

    EXPECT_FALSE(registry.has("OldFormat"));
    EXPECT_TRUE(registry.has("NewFormat"));

    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 9: NoFallbackPortDerivation — exposes is single source of truth
// ==================================================================

TEST(LibraryV2, NoFallbackPortDerivation) {
    // A composite blueprint with BlueprintInput/BlueprintOutput nodes
    // but NO explicit exposes should result in ZERO ports.
    // This verifies there is no silent fallback that scans nodes.
    FlatBlueprint bp;
    bp.version = 2;
    bp.meta.name = "NoExposes";
    bp.meta.cpp_class = false;
    bp.meta.domains = {"Electrical"};

    FlatNode input_node;
    input_node.type = "BlueprintInput";
    input_node.params["exposed_type"] = "V";
    input_node.params["exposed_direction"] = "In";
    bp.nodes["vin"] = input_node;

    FlatNode output_node;
    output_node.type = "BlueprintOutput";
    output_node.params["exposed_type"] = "V";
    output_node.params["exposed_direction"] = "Out";
    bp.nodes["vout"] = output_node;

    // Exposes is intentionally empty — no ports declared
    ASSERT_TRUE(bp.exposes.empty());

    TypeDefinition td = flat_to_type_definition(bp);

    // Must have zero ports — no silent fallback from nodes
    EXPECT_TRUE(td.ports.empty())
        << "flat_to_type_definition must NOT silently derive ports from nodes; "
           "exposes is the single source of truth";

    // Devices should still be loaded
    ASSERT_EQ(td.devices.size(), 2u);
}

// ==================================================================
// Test 10: ExposesPopulatedPortsWork — explicit exposes → correct ports
// ==================================================================

TEST(LibraryV2, ExposesPopulatedPortsWork) {
    // A composite blueprint with explicit exposes should produce correct ports.
    FlatBlueprint bp;
    bp.version = 2;
    bp.meta.name = "WithExposes";
    bp.meta.cpp_class = false;
    bp.meta.domains = {"Electrical"};

    bp.exposes["power_in"] = FlatPort{"In", "V", std::nullopt};
    bp.exposes["signal_out"] = FlatPort{"Out", "Bool", std::nullopt};

    FlatNode input_node;
    input_node.type = "BlueprintInput";
    input_node.display_name = "power_in";
    bp.nodes["bp_in_1"] = input_node;

    FlatNode output_node;
    output_node.type = "BlueprintOutput";
    output_node.display_name = "signal_out";
    bp.nodes["bp_out_1"] = output_node;

    TypeDefinition td = flat_to_type_definition(bp);

    ASSERT_EQ(td.ports.size(), 2u);
    EXPECT_EQ(td.ports.at("power_in").direction, PortDirection::In);
    EXPECT_EQ(td.ports.at("power_in").type, PortType::V);
    EXPECT_EQ(td.ports.at("signal_out").direction, PortDirection::Out);
    EXPECT_EQ(td.ports.at("signal_out").type, PortType::Bool);
}

// ==================================================================
// Test 11: ExposesPopulatedViaRegistry — .blueprint file with exposes
//          loads ports correctly through the registry pipeline
// ==================================================================

TEST(LibraryV2, ExposesPopulatedViaRegistry) {
    auto temp_dir = std::filesystem::temp_directory_path() / "test_library_v2_exposes";
    std::filesystem::create_directories(temp_dir);

    // Write a composite .blueprint with explicit exposes
    {
        FlatBlueprint bp;
        bp.meta.name = "SubCircuit";
        bp.meta.cpp_class = false;
        bp.meta.domains = {"Electrical"};

        bp.exposes["v_in"] = FlatPort{"In", "V", std::nullopt};
        bp.exposes["v_out"] = FlatPort{"Out", "V", std::nullopt};

        FlatNode input_node;
        input_node.type = "BlueprintInput";
        input_node.params["exposed_type"] = "V";
        bp.nodes["vin"] = input_node;

        FlatNode output_node;
        output_node.type = "BlueprintOutput";
        output_node.params["exposed_type"] = "V";
        bp.nodes["vout"] = output_node;

        FlatWire wire;
        wire.id = "w0";
        wire.from = {"vin", "port"};
        wire.to = {"vout", "port"};
        bp.wires.push_back(wire);

        std::ofstream f(temp_dir / "SubCircuit.blueprint");
        f << serialize_flat_blueprint(bp);
    }

    TypeRegistry registry = load_type_registry(temp_dir.string());
    auto* def = registry.get("SubCircuit");
    ASSERT_NE(def, nullptr);

    // Ports must come from the exposes section
    ASSERT_EQ(def->ports.size(), 2u);
    EXPECT_EQ(def->ports.at("v_in").direction, PortDirection::In);
    EXPECT_EQ(def->ports.at("v_in").type, PortType::V);
    EXPECT_EQ(def->ports.at("v_out").direction, PortDirection::Out);
    EXPECT_EQ(def->ports.at("v_out").type, PortType::V);

    std::filesystem::remove_all(temp_dir);
}

// ==================================================================
// Test 12: SimExportRewritesMismatchedPorts — with Option B rename,
//          BlueprintInput/Output nodes are renamed to their expose
//          names in the TypeDefinition. Wire rewriting uses the
//          expose name directly as the internal node key.
//          Regression test for the RUG84_1 bug.
// ==================================================================

TEST(LibraryV2, SimExportRewritesMismatchedPorts) {
    // ----------------------------------------------------------
    // Build a Blueprint that simulates a parent containing:
    //   - bat_main_1 (Battery)
    //   - sub_1 (collapsed sub-blueprint, expandable)
    //   - internal nodes: sub_1:v, sub_1:Comp  (Option B: renamed to expose names)
    //   - wire: bat_main_1.v_out → sub_1.v
    //
    // to_simulator_json() must rewrite the wire target to:
    //   "sub_1:v.ext" (expose name == internal node key)
    // ----------------------------------------------------------

    Blueprint bp;
    auto& I = bp.interner();

    // Battery node
    {
        Node n;
        n.id = I.intern("bat_main_1");
        n.name = "bat_main_1";
        n.type_name = "Battery";
        n.inputs.push_back(EditorPort{I.intern("v_in"), PortSide::Input, PortType::V});
        n.outputs.push_back(EditorPort{I.intern("v_out"), PortSide::Output, PortType::V});
        bp.nodes.push_back(std::move(n));
    }

    // Expandable sub-blueprint node (the collapsed UI node)
    {
        Node n;
        n.id = I.intern("sub_1");
        n.name = "sub_1";
        n.type_name = "MismatchSub";
        n.expandable = true;
        n.inputs.push_back(EditorPort{I.intern("v"), PortSide::Input, PortType::V});
        n.outputs.push_back(EditorPort{I.intern("Comp"), PortSide::Output, PortType::Any});
        bp.nodes.push_back(std::move(n));
    }

    // Internal nodes (expanded from sub-blueprint — Option B: already renamed to expose names)
    {
        Node n;
        n.id = I.intern("sub_1:v");
        n.name = "sub_1:v";
        n.type_name = "BlueprintInput";
        n.group_id = "sub_1";
        n.inputs.push_back(EditorPort{I.intern("ext"), PortSide::Input, PortType::V});
        n.outputs.push_back(EditorPort{I.intern("port"), PortSide::Output, PortType::V});
        bp.nodes.push_back(std::move(n));
    }
    {
        Node n;
        n.id = I.intern("sub_1:Comp");
        n.name = "sub_1:Comp";
        n.type_name = "BlueprintOutput";
        n.group_id = "sub_1";
        n.inputs.push_back(EditorPort{I.intern("port"), PortSide::Input, PortType::Any});
        n.outputs.push_back(EditorPort{I.intern("ext"), PortSide::Output, PortType::Any});
        bp.nodes.push_back(std::move(n));
    }

    // Wire: bat_main_1.v_out → sub_1.v (uses expose port name)
    {
        Wire wire;
        wire.id = I.intern("wire_0");
        wire.start.node_id = I.intern("bat_main_1");
        wire.start.port_name = I.intern("v_out");
        wire.end.node_id = I.intern("sub_1");
        wire.end.port_name = I.intern("v");
        bp.wires.push_back(std::move(wire));
    }

    // Sub-blueprint instance (no port_to_node_key needed with Option B)
    {
        SubBlueprintInstance sbi;
        sbi.id = "sub_1";
        sbi.type_name = "MismatchSub";
        sbi.internal_node_ids = {"sub_1:v", "sub_1:Comp"};
        bp.sub_blueprint_instances.push_back(std::move(sbi));
    }

    bp.rebuild_all_indices();

    // Export to simulator JSON
    std::string sim_json = bp.to_simulator_json();
    auto j = nlohmann::json::parse(sim_json);
    ASSERT_TRUE(j.contains("connections"));
    auto& conns = j["connections"];

    // Find the connection from bat_main_1 to the sub-blueprint
    bool found_correct = false;
    for (const auto& conn : conns) {
        std::string to = conn.at("to").get<std::string>();
        if (to == "sub_1:v.ext") {
            found_correct = true;
            EXPECT_EQ(conn.at("from").get<std::string>(), "bat_main_1.v_out");
        }
    }

    EXPECT_TRUE(found_correct)
        << "Expected connection to 'sub_1:v.ext' not found in simulator JSON.\n"
        << "Connections JSON: " << j["connections"].dump(2);
}
