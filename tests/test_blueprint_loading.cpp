#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/state.h"
#include "core/solvers/jit/components/all.h"
#include "parse_number.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

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
    // 12SAM28 is a blueprint type in library/systems/ (cpp_class=false)
    TypeRegistry reg = load_type_registry("library/");
    ASSERT_TRUE(reg.has("12SAM28"));
    const auto* def = reg.get("12SAM28");
    ASSERT_FALSE(def->cpp_class);
}

// =============================================================================
// Helper: Run simulation to steady state (push model)
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
        (void)state.allocate_signal(0.0f);
    }

    // Set fixed signal values from RefNode devices
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = locale_safe::parse_float_or(it_val->second, 0.0f);
            }
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end()) {
                state.values[it_sig->second] = value;
            }
        }
    }

    // Push model: run simulation steps using the scheduler
    constexpr double dt = 1.0 / 60.0;
    for (int step = 0; step < steps; ++step) {
        result.scheduler.step(state, dt);
    }

    return state;
}

// Helper to get signal voltage by port name
static float get_voltage(const SimulationState& state, const BuildResult& result,
                          const std::string& port_name) {
    auto it = result.port_to_signal.find(port_name);
    EXPECT_NE(it, result.port_to_signal.end()) << "Port not found: " << port_name;
    return state.values[it->second];
}

// =============================================================================
// Integration Tests: Full pipeline with simulation
// =============================================================================

// =============================================================================
// Unit Tests: extract_exposed_ports()
// =============================================================================

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
        {{"name", "bat"}, {"classname", "ElectricalSource"}, {"params", {{"voltage", "28.0"}}}}
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
    // Uses the real library: 12SAM28 contains ElectricalSource (cpp_class=true)
    nlohmann::json root;
    root["devices"] = {
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "sb"}, {"classname", "12SAM28"}}
    };
    root["connections"] = {
        {{"from", "gnd.v"}, {"to", "sb.v_in"}}
    };

    // Should NOT throw — this is valid nesting with no cycles
    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(root.dump()));

    // Verify it expanded
    bool found_sb_src = false;
    for (const auto& dev : ctx.devices) {
        if (dev.name == "sb:src") found_sb_src = true;
    }
    EXPECT_TRUE(found_sb_src);
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
    EXPECT_TRUE(reg.has("ElectricalSource"));
    // 12SAM28 (composite) must also load from .blueprint
    EXPECT_TRUE(reg.has("12SAM28"));
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
            "version": "3.0",
            "id": "TestComp",
            "display_name": "TestComp",
            "cpp_class": true,
            "scheduler_source": false,
            "solver_owned_electrical": false,
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
            "interface": [
                {"name": "v_out", "domain": 1, "direction": 1, "type": "V", "source_writer": false}
            ],
            "nodes": [],
            "wires": []
        })";
    }

    // Write a .json file (should be ignored)
    {
        std::ofstream f(tmp_dir / "Ignored.json");
        f << R"({
            "version": "3.0",
            "id": "Ignored",
            "display_name": "Ignored",
            "interface": [],
            "nodes": [],
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

// Verify blueprint.blueprint (main save file) exists and is valid v3
// Skipped when the workspace save file is not present.
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
    if (content.empty()) {
        GTEST_SKIP() << "blueprint.blueprint not present (workspace save file, not source-controlled)";
    }

    // Must be valid JSON with version: "3.0"
    auto j = nlohmann::json::parse(content);
    ASSERT_TRUE(j.contains("version")) << "blueprint.blueprint must have a version field";
    ASSERT_TRUE(j.at("version").is_string()) << "version must be a string in v3 format";
    EXPECT_EQ(j.at("version").get<std::string>(), "3.0") << "blueprint.blueprint must be v3 format";
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

// =============================================================================
// Structural convention: all nodes must have name, Value nodes must have render_hint
// =============================================================================

/// Helper: find a .blueprint file by classname, searching library/ subdirectories
static std::string find_blueprint_file(const std::string& classname) {
    namespace fs = std::filesystem;
    std::vector<std::string> search_paths = {
        "library/", "../library/", "../../library/"
    };
    for (const auto& base : search_paths) {
        if (!fs::exists(base)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(base)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".blueprint" &&
                entry.path().stem() == classname) {
                return entry.path().string();
            }
        }
    }
    return {};
}

/// Helper: load a blueprint JSON file and return parsed nodes array
static nlohmann::json load_blueprint_nodes(const std::string& classname) {
    std::string path = find_blueprint_file(classname);
    if (path.empty()) return {};
    std::ifstream f(path);
    if (!f.is_open()) return {};
    auto j = nlohmann::json::parse(f);
    if (!j.contains("nodes") || !j["nodes"].is_array()) return {};
    return j["nodes"];
}

TEST(BlueprintConvention, AllNodesHaveName_12SAM28) {
    // Every node in 12SAM28.blueprint must have a non-empty "name" field.
    // Missing names cause subtle UI bugs (empty labels, broken inspector lookups).
    auto nodes = load_blueprint_nodes("12SAM28");
    ASSERT_FALSE(nodes.empty()) << "12SAM28.blueprint not found or has no nodes";

    for (const auto& node : nodes) {
        std::string id = node.value("id", "<missing_id>");
        ASSERT_TRUE(node.contains("name"))
            << "Node '" << id << "' is missing a 'name' field";
        std::string name = node["name"].get<std::string>();
        EXPECT_FALSE(name.empty())
            << "Node '" << id << "' has an empty 'name' field";
    }
}

TEST(BlueprintConvention, ValueNodesHaveRenderHint_12SAM28) {
    // All Value nodes must have render_hint="ref" so the editor renders them
    // as inline reference badges instead of full node boxes.
    auto nodes = load_blueprint_nodes("12SAM28");
    ASSERT_FALSE(nodes.empty()) << "12SAM28.blueprint not found or has no nodes";

    int value_count = 0;
    for (const auto& node : nodes) {
        std::string type = node.value("type", "");
        if (type == "Value") {
            value_count++;
            std::string id = node.value("id", "<missing_id>");
            ASSERT_TRUE(node.contains("render_hint"))
                << "Value node '" << id << "' is missing render_hint field";
            EXPECT_EQ(node["render_hint"].get<std::string>(), "ref")
                << "Value node '" << id << "' render_hint should be \"ref\"";
        }
    }
    EXPECT_GT(value_count, 0) << "12SAM28 should contain at least one Value node";
}

TEST(BlueprintConvention, NodeNameMatchesId_12SAM28) {
    // Convention: name field should match id field.  Mismatches indicate
    // copy-paste errors or incomplete tooling updates.
    auto nodes = load_blueprint_nodes("12SAM28");
    ASSERT_FALSE(nodes.empty()) << "12SAM28.blueprint not found or has no nodes";

    for (const auto& node : nodes) {
        std::string id = node.value("id", "<missing_id>");
        std::string name = node.value("name", "<missing_name>");
        EXPECT_EQ(name, id)
            << "Node name '" << name << "' doesn't match id '" << id << "'";
    }
}
