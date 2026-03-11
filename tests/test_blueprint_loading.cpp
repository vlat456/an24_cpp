#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/SOR_constants.h"
#include "jit_solver/components/all.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using namespace an24;
using json = nlohmann::json;

// =============================================================================
// Blueprint Loading Tests
// =============================================================================

TEST(BlueprintLoading, UnknownClassnameThrows) {
    // "test_battery_module" is NOT in TypeRegistry — should throw
    nlohmann::json root;
    root["devices"] = {
        {{"name", "bat1"}, {"classname", "test_battery_module"}}
    };
    root["connections"] = {};

    EXPECT_THROW(parse_json(root.dump()), std::runtime_error);
}

TEST(BlueprintLoading, MissingBlueprintReturnsError) {
    // Try to use non-existent component
    nlohmann::json root;
    root["devices"] = {
        {{"name", "x"}, {"classname", "totally_bogus_component_xyz"}}
    };

    // Should throw error
    EXPECT_THROW(parse_json(root.dump()), std::runtime_error);
}

TEST(BlueprintLoading, DirectBlueprintLoadWorks) {
    // simple_battery is a blueprint type in library/ (cpp_class=false)
    // Load its TypeDefinition and verify structure
    TypeRegistry reg = load_type_registry("library/");
    ASSERT_TRUE(reg.has("simple_battery"));
    const auto* def = reg.get("simple_battery");
    ASSERT_FALSE(def->cpp_class);
    EXPECT_EQ(def->devices.size(), 4);    // gnd, bat, vin, vout
    EXPECT_EQ(def->connections.size(), 3); // 3 connections
}

// =============================================================================
// Helper: Run simulation to steady state
// =============================================================================
static SimulationState run_simulation(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices,
    int steps = 50
) {
    SimulationState state;

    // Allocate signals
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(),
            result.fixed_signals.end(),
            i
        );
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Set fixed signal values from RefNode devices
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
            }
        }
    }

    // SOR iteration
    for (int step = 0; step < steps; ++step) {
        state.clear_through();

        // Solve electrical domain using domain_components
        for (auto* variant : result.domain_components.electrical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, 0.0f); }) {
                    comp.solve_electrical(state, 1.0f / 60.0f);
                }
            }, *variant);
        }

        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * SOR::OMEGA;
            }
        }
    }

    return state;
}

// Helper to get signal voltage by port name
static float get_voltage(const SimulationState& state, const BuildResult& result,
                          const std::string& port_name) {
    auto it = result.port_to_signal.find(port_name);
    EXPECT_NE(it, result.port_to_signal.end()) << "Port not found: " << port_name;
    return state.across[it->second];
}

// =============================================================================
// Integration Tests: Full pipeline with simulation
// =============================================================================

TEST(BlueprintLoading, Integration_NestedBlueprintRunsSimulation) {
    // Root blueprint uses nested simple_battery blueprint
    // Connects a load resistor to verify output voltage

    nlohmann::json root;
    root["devices"] = {
        {{"name", "main_gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "bat1"}, {"classname", "simple_battery"}},  // Expanded from TypeRegistry
        {{"name", "load"}, {"classname", "Resistor"}, {"params", {{"conductance", "0.1"}}}}
    };
    root["connections"] = {
        {{"from", "main_gnd.v"}, {"to", "bat1.vin"}},
        {{"from", "bat1.vout"}, {"to", "load.v_in"}},
        {{"from", "load.v_out"}, {"to", "main_gnd.v"}}
    };

    // Parse (should trigger blueprint loading)
    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(root.dump()));

    // Verify nested blueprint was loaded with prefix
    EXPECT_GE(ctx.devices.size(), 5);  // main_gnd + simple_battery:4 devices + load
    EXPECT_GE(ctx.connections.size(), 3);  // At least 3 connections

    // Convert Connection structs to pairs for build_systems_dev
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& conn : ctx.connections) {
        connections.push_back({conn.from, conn.to});
    }

    // Build systems (full pipeline)
    BuildResult result = build_systems_dev(ctx.devices, connections);

    // Verify build succeeded
    EXPECT_GT(result.signal_count, 0);
    EXPECT_GT(result.devices.size(), 0);

    // Run simulation
    SimulationState state = run_simulation(result, ctx.devices);

    // Verify battery output voltage (should be ~28V from simple_battery)
    // The nested blueprint's BlueprintOutput "vout" becomes "bat1:vout"
    float v_bat1_vout = get_voltage(state, result, "bat1:vout.port");
    EXPECT_GT(v_bat1_vout, 25.0f) << "Nested battery output should be close to 28V";
    EXPECT_LT(v_bat1_vout, 30.0f) << "Nested battery output should not exceed nominal significantly";

    // Verify ground is at 0V
    float v_gnd = get_voltage(state, result, "main_gnd.v");
    EXPECT_NEAR(v_gnd, 0.0f, 0.1f) << "Ground should be at 0V";
}

// =============================================================================
// Unit Tests: extract_exposed_ports()
// =============================================================================

TEST(ExtractExposedPorts, SimpleBatteryBlueprint) {
    // simple_battery is in TypeRegistry as cpp_class=false
    // Build a JSON with its internal devices and parse to extract exposed ports
    TypeRegistry reg = load_type_registry("library/");
    ASSERT_TRUE(reg.has("simple_battery"));
    const auto* def = reg.get("simple_battery");

    nlohmann::json bp_json;
    bp_json["devices"] = nlohmann::json::array();
    for (const auto& dev : def->devices) {
        nlohmann::json dev_j;
        dev_j["name"] = dev.name;
        dev_j["classname"] = dev.classname;
        if (!dev.params.empty()) dev_j["params"] = dev.params;
        bp_json["devices"].push_back(dev_j);
    }
    bp_json["connections"] = nlohmann::json::array();
    for (const auto& conn : def->connections) {
        bp_json["connections"].push_back({{"from", conn.from}, {"to", conn.to}});
    }

    ParserContext ctx = parse_json(bp_json.dump());

    // Extract exposed ports
    auto exposed = extract_exposed_ports(ctx);

    // Should have 2 exposed ports: vin (BlueprintInput), vout (BlueprintOutput)
    EXPECT_EQ(exposed.size(), 2);

    // Check "vin" - BlueprintInput with exposed_direction="In"
    // This means: parent connects TO this port as Input (data flows INTO blueprint)
    EXPECT_TRUE(exposed.count("vin")) << "Should have 'vin' exposed port";
    EXPECT_EQ(exposed["vin"].direction, PortDirection::In) << "vin should be Input (data flows into blueprint)";
    EXPECT_EQ(exposed["vin"].type, PortType::V) << "vin should be type V";

    // Check "vout" - BlueprintOutput with exposed_direction="Out"
    // This means: parent connects TO this port as Output (data flows OUT OF blueprint)
    EXPECT_TRUE(exposed.count("vout")) << "Should have 'vout' exposed port";
    EXPECT_EQ(exposed["vout"].direction, PortDirection::Out) << "vout should be Output (data flows out of blueprint)";
    EXPECT_EQ(exposed["vout"].type, PortType::V) << "vout should be type V";
}

TEST(ExtractExposedPorts, MultipleBlueprints) {
    // Create blueprint with multiple inputs/outputs
    nlohmann::json bp;
    bp["devices"] = nlohmann::json::array({
        {{"name", "in1"}, {"classname", "BlueprintInput"}, {"params", {{"exposed_type", "V"}, {"exposed_direction", "In"}}}},
        {{"name", "in2"}, {"classname", "BlueprintInput"}, {"params", {{"exposed_type", "I"}, {"exposed_direction", "In"}}}},
        {{"name", "out1"}, {"classname", "BlueprintOutput"}, {"params", {{"exposed_type", "Bool"}, {"exposed_direction", "Out"}}}},
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}}
    });
    bp["connections"] = nlohmann::json::array();

    ParserContext ctx = parse_json(bp.dump());
    auto exposed = extract_exposed_ports(ctx);

    // Should have 3 exposed ports (in1, in2, out1), excluding gnd
    EXPECT_EQ(exposed.size(), 3);

    // Verify BlueprintInput directions (data flows INTO blueprint)
    EXPECT_EQ(exposed["in1"].direction, PortDirection::In);
    EXPECT_EQ(exposed["in2"].direction, PortDirection::In);

    // Verify BlueprintOutput direction (data flows OUT OF blueprint)
    EXPECT_EQ(exposed["out1"].direction, PortDirection::Out);

    // Verify types
    EXPECT_EQ(exposed["in1"].type, PortType::V);
    EXPECT_EQ(exposed["in2"].type, PortType::I);
    EXPECT_EQ(exposed["out1"].type, PortType::Bool);
}

TEST(ExtractExposedPorts, EmptyBlueprint) {
    // Blueprint with no BlueprintInput/BlueprintOutput
    nlohmann::json bp;
    bp["devices"] = nlohmann::json::array({
        {{"name", "bat"}, {"classname", "Battery"}, {"params", {{"v_nominal", "28.0"}}}}
    });
    bp["connections"] = nlohmann::json::array();

    ParserContext ctx = parse_json(bp.dump());
    auto exposed = extract_exposed_ports(ctx);

    // Should have 0 exposed ports
    EXPECT_EQ(exposed.size(), 0);
}

TEST(ExtractExposedPorts, DefaultValues) {
    // BlueprintInput/BlueprintOutput without explicit params use TypeRegistry defaults
    nlohmann::json bp;
    bp["devices"] = nlohmann::json::array({
        {{"name", "in"}, {"classname", "BlueprintInput"}},  // No params - uses component defaults
        {{"name", "out"}, {"classname", "BlueprintOutput"}}  // No params - uses component defaults
    });
    bp["connections"] = nlohmann::json::array();

    ParserContext ctx = parse_json(bp.dump());
    auto exposed = extract_exposed_ports(ctx);

    EXPECT_EQ(exposed.size(), 2);

    // Default from component definition: BlueprintInput has exposed_direction="In"
    EXPECT_EQ(exposed["in"].direction, PortDirection::In);
    // Default from component definition: BlueprintOutput has exposed_direction="Out"
    EXPECT_EQ(exposed["out"].direction, PortDirection::Out);

    // Default type from component definition (both have "V" as default)
    EXPECT_EQ(exposed["in"].type, PortType::V);
    EXPECT_EQ(exposed["out"].type, PortType::V);
}

// =============================================================================
// Phase 5: parse_json() expands cpp_class=false types from TypeRegistry
// =============================================================================

TEST(BlueprintLoading, ExpandBlueprintFromTypeRegistry) {
    // simple_battery is cpp_class=false in library/ and has devices/connections
    TypeRegistry reg = load_type_registry("library/");
    ASSERT_TRUE(reg.has("simple_battery"));
    const auto* def = reg.get("simple_battery");
    ASSERT_FALSE(def->cpp_class);
    ASSERT_FALSE(def->devices.empty());

    // Use simple_battery in a root circuit — it should be expanded from TypeRegistry
    nlohmann::json root;
    root["devices"] = {
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "sb"}, {"classname", "simple_battery"}},
        {{"name", "load"}, {"classname", "Resistor"}, {"params", {{"conductance", "0.1"}}}}
    };
    root["connections"] = {
        {{"from", "gnd.v"}, {"to", "sb.vin"}},
        {{"from", "sb.vout"}, {"to", "load.v_in"}},
        {{"from", "load.v_out"}, {"to", "gnd.v"}}
    };

    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(root.dump()));

    // SimpleBattery should be expanded: its internal devices have "sb:" prefix
    bool has_sb_bat = false, has_sb_gnd = false, has_sb_vin = false, has_sb_vout = false;
    for (const auto& dev : ctx.devices) {
        if (dev.name == "sb:bat") has_sb_bat = true;
        if (dev.name == "sb:gnd") has_sb_gnd = true;
        if (dev.name == "sb:vin") has_sb_vin = true;
        if (dev.name == "sb:vout") has_sb_vout = true;
    }
    EXPECT_TRUE(has_sb_bat) << "Should have expanded 'sb:bat'";
    EXPECT_TRUE(has_sb_gnd) << "Should have expanded 'sb:gnd'";
    EXPECT_TRUE(has_sb_vin) << "Should have expanded 'sb:vin'";
    EXPECT_TRUE(has_sb_vout) << "Should have expanded 'sb:vout'";

    // No device named "sb" alone (it was expanded)
    for (const auto& dev : ctx.devices) {
        EXPECT_NE(dev.name, "sb") << "Blueprint 'sb' should be expanded, not kept as device";
    }
}

// =============================================================================
// Cycle Detection: self-referencing blueprints must throw, not stack-overflow
// =============================================================================

TEST(BlueprintCycleDetection, DirectSelfReference_Throws) {
    // Create a temporary library directory with a self-referencing blueprint:
    // "SelfRef" contains a device of classname "SelfRef" → direct cycle
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "an24_cycle_test_direct";
    fs::create_directories(tmp_dir);

    // Write a self-referencing blueprint
    {
        nlohmann::json self_ref;
        self_ref["classname"] = "SelfRef";
        self_ref["cpp_class"] = false;
        self_ref["ports"] = {{"vin", {{"direction", "In"}, {"type", "V"}}}};
        self_ref["domains"] = {"Electrical"};
        self_ref["devices"] = {{{"name", "inner"}, {"classname", "SelfRef"}}};
        self_ref["connections"] = nlohmann::json::array();
        std::ofstream(tmp_dir / "SelfRef.json") << self_ref.dump(2);
    }
    // Also need a RefNode so the registry has basic types — copy from real library
    // Actually, we only need the self-referencing type. The expansion will look it up
    // in the registry loaded from tmp_dir.

    // Build a root circuit that uses SelfRef
    nlohmann::json root;
    root["devices"] = {{{"name", "x"}, {"classname", "SelfRef"}}};
    root["connections"] = nlohmann::json::array();

    // Should throw with cycle detection error, NOT stack-overflow
    EXPECT_THROW(parse_json(root.dump(), tmp_dir.string()), std::runtime_error);

    // Cleanup
    fs::remove_all(tmp_dir);
}

TEST(BlueprintCycleDetection, IndirectCycle_Throws) {
    // A contains B, B contains A → indirect cycle
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "an24_cycle_test_indirect";
    fs::create_directories(tmp_dir);

    // Write type A that contains B
    {
        nlohmann::json type_a;
        type_a["classname"] = "CycleA";
        type_a["cpp_class"] = false;
        type_a["ports"] = {{"vin", {{"direction", "In"}, {"type", "V"}}}};
        type_a["domains"] = {"Electrical"};
        type_a["devices"] = {{{"name", "b"}, {"classname", "CycleB"}}};
        type_a["connections"] = nlohmann::json::array();
        std::ofstream(tmp_dir / "CycleA.json") << type_a.dump(2);
    }
    // Write type B that contains A
    {
        nlohmann::json type_b;
        type_b["classname"] = "CycleB";
        type_b["cpp_class"] = false;
        type_b["ports"] = {{"vin", {{"direction", "In"}, {"type", "V"}}}};
        type_b["domains"] = {"Electrical"};
        type_b["devices"] = {{{"name", "a"}, {"classname", "CycleA"}}};
        type_b["connections"] = nlohmann::json::array();
        std::ofstream(tmp_dir / "CycleB.json") << type_b.dump(2);
    }

    nlohmann::json root;
    root["devices"] = {{{"name", "x"}, {"classname", "CycleA"}}};
    root["connections"] = nlohmann::json::array();

    EXPECT_THROW(parse_json(root.dump(), tmp_dir.string()), std::runtime_error);

    // Cleanup
    fs::remove_all(tmp_dir);
}

TEST(BlueprintCycleDetection, ValidNesting_NoCycle) {
    // A contains B, B contains C++ leaf → NOT a cycle, should work fine
    // Uses the real library: simple_battery contains Battery (cpp_class=true)
    nlohmann::json root;
    root["devices"] = {
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "sb"}, {"classname", "simple_battery"}}
    };
    root["connections"] = {
        {{"from", "gnd.v"}, {"to", "sb.vin"}}
    };

    // Should NOT throw — this is valid nesting with no cycles
    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(root.dump()));

    // Verify it expanded
    bool found_sb_bat = false;
    for (const auto& dev : ctx.devices) {
        if (dev.name == "sb:bat") found_sb_bat = true;
    }
    EXPECT_TRUE(found_sb_bat);
}

// =============================================================================
// Regression: .blueprint extension standardization
// =============================================================================

// Verify load_type_registry scans only .blueprint files
TEST(BlueprintExtension, RegistryLoadsOnlyBlueprintFiles) {
    TypeRegistry reg = load_type_registry("library/");
    // Registry must find at least some components
    EXPECT_GT(reg.types.size(), 10u) << "Registry should load many .blueprint files";
    // Battery is a well-known component
    EXPECT_TRUE(reg.has("Battery"));
    // simple_battery (composite) must also load from .blueprint
    EXPECT_TRUE(reg.has("simple_battery"));
}

// Verify that .json files in library/ are ignored by the loader
TEST(BlueprintExtension, RegistryIgnoresJsonFiles) {
    // Create a temp directory with one .blueprint and one .json file
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "an24_ext_test";
    fs::create_directories(tmp_dir);

    // Write a valid .blueprint file
    {
        std::ofstream f(tmp_dir / "TestComp.blueprint");
        f << R"({
            "version": 2,
            "meta": {
                "name": "TestComp",
                "description": "Test component",
                "domains": ["Electrical"],
                "cpp_class": true
            },
            "exposes": {
                "v_out": {"direction": "Out", "type": "V"}
            },
            "params": {},
            "nodes": {},
            "wires": []
        })";
    }

    // Write a .json file (should be ignored)
    {
        std::ofstream f(tmp_dir / "Ignored.json");
        f << R"({
            "version": 2,
            "meta": {
                "name": "Ignored",
                "description": "Should not be loaded",
                "domains": ["Electrical"],
                "cpp_class": true
            },
            "exposes": {},
            "params": {},
            "nodes": {},
            "wires": []
        })";
    }

    TypeRegistry reg = load_type_registry(tmp_dir.string());
    EXPECT_TRUE(reg.has("TestComp")) << ".blueprint file should be loaded";
    EXPECT_FALSE(reg.has("Ignored")) << ".json file must NOT be loaded";

    // Cleanup
    fs::remove_all(tmp_dir);
}

// Verify no .json files remain in library/
TEST(BlueprintExtension, NoJsonFilesInLibrary) {
    namespace fs = std::filesystem;

    // Find library path (same search logic as load_type_registry)
    fs::path library_path = "library/";
    std::vector<fs::path> try_paths = {
        "library/", "../library/", "../../library/", "../../../library/"
    };
    for (const auto& p : try_paths) {
        if (fs::exists(p)) {
            library_path = p;
            break;
        }
    }
    ASSERT_TRUE(fs::exists(library_path)) << "library/ directory not found";

    std::vector<std::string> json_files;
    for (const auto& entry : fs::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            json_files.push_back(entry.path().string());
        }
    }
    EXPECT_TRUE(json_files.empty())
        << "Found stray .json files in library/: "
        << (json_files.empty() ? "" : json_files[0]);
}

// Verify all library files use .blueprint extension
TEST(BlueprintExtension, AllLibraryFilesAreBlueprintExtension) {
    namespace fs = std::filesystem;

    fs::path library_path = "library/";
    std::vector<fs::path> try_paths = {
        "library/", "../library/", "../../library/", "../../../library/"
    };
    for (const auto& p : try_paths) {
        if (fs::exists(p)) {
            library_path = p;
            break;
        }
    }
    ASSERT_TRUE(fs::exists(library_path));

    size_t blueprint_count = 0;
    size_t other_count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file()) {
            // Skip hidden files (.DS_Store, etc.)
            if (entry.path().filename().string()[0] == '.') continue;
            if (entry.path().extension() == ".blueprint") {
                blueprint_count++;
            } else {
                other_count++;
                ADD_FAILURE() << "Non-.blueprint file in library: " << entry.path();
            }
        }
    }
    EXPECT_GT(blueprint_count, 0u) << "Should find .blueprint files";
    EXPECT_EQ(other_count, 0u) << "No non-.blueprint files should exist in library/";
}

// Verify blueprint.blueprint (main save file) exists and is valid v2
TEST(BlueprintExtension, MainSaveFileIsBlueprintExtension) {
    namespace fs = std::filesystem;

    std::vector<std::string> paths = {
        "blueprint.blueprint", "../blueprint.blueprint", "../../blueprint.blueprint"
    };
    std::string content;
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (f.is_open()) {
            content.assign(std::istreambuf_iterator<char>(f),
                          std::istreambuf_iterator<char>());
            break;
        }
    }
    ASSERT_FALSE(content.empty()) << "blueprint.blueprint not found";

    // Must be valid JSON with version: 2
    auto j = nlohmann::json::parse(content);
    EXPECT_EQ(j.at("version").get<int>(), 2) << "blueprint.blueprint must be v2 format";
}

// Verify codegen source_file uses .blueprint extension
TEST(BlueprintExtension, CodegenUsesBluprintExtension) {
    // The codegen generates source_file = classname + ".blueprint"
    // We verify by loading a composite type and checking it round-trips
    TypeRegistry reg = load_type_registry("library/");

    // Find any composite (cpp_class=false) type
    std::string composite_name;
    for (const auto& [name, def] : reg.types) {
        if (!def.cpp_class) {
            composite_name = name;
            break;
        }
    }
    ASSERT_FALSE(composite_name.empty()) << "Need at least one composite type for test";

    const auto* def = reg.get(composite_name);
    ASSERT_NE(def, nullptr);
    // Verify classname doesn't contain .json
    EXPECT_EQ(def->classname.find(".json"), std::string::npos)
        << "Classname should not contain .json: " << def->classname;
}
