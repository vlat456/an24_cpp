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
// Helper: Create temporary blueprint file
// =============================================================================
static void create_test_blueprint(const std::string& path) {
    // Don't overwrite existing blueprint
    if (std::filesystem::exists(path)) {
        return;
    }

    nlohmann::json blueprint;
    blueprint["description"] = "Test battery module";
    blueprint["devices"] = {
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "bat"}, {"classname", "Battery"}, {"params", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}}},
        {{"name", "vin"}, {"classname", "BlueprintInput"}, {"params", {{"exposed_type", "V"}, {"exposed_direction", "In"}}}},
        {{"name", "vout"}, {"classname", "BlueprintOutput"}, {"params", {{"exposed_type", "V"}, {"exposed_direction", "Out"}}}}
    };
    blueprint["connections"] = {
        {{"from", "vin.port"}, {"to", "bat.v_in"}},
        {{"from", "bat.v_out"}, {"to", "vout.port"}},
        {{"from", "gnd.v"}, {"to", "vin.port"}}
    };

    // Create directory if needed
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream file(path);
    file << blueprint.dump(2);
}

// =============================================================================
// Phase 2 Tests: Blueprint Loading Fallback
// =============================================================================

TEST(BlueprintLoading, FallbackToBlueprintWhenNotInRegistry) {
    // Create test blueprint in blueprints/
    std::string blueprint_path = "blueprints/test_battery_module.json";
    create_test_blueprint(blueprint_path);

    // Root blueprint uses "test_battery_module" (not in component registry)
    nlohmann::json root;
    root["devices"] = {
        {{"name", "bat1"}, {"classname", "test_battery_module"}}
    };
    root["connections"] = {};

    // Parse - should automatically load nested blueprint
    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(root.dump()));

    // Verify devices have prefix
    EXPECT_FALSE(ctx.devices.empty()) << "Should have loaded nested devices";

    // Check for prefixed device names
    bool has_gnd = false, has_bat = false, has_vin = false, has_vout = false;
    for (const auto& dev : ctx.devices) {
        if (dev.name == "bat1:gnd") has_gnd = true;
        if (dev.name == "bat1:bat") has_bat = true;
        if (dev.name == "bat1:vin") has_vin = true;
        if (dev.name == "bat1:vout") has_vout = true;
    }

    EXPECT_TRUE(has_gnd) << "Should have 'bat1:gnd' device";
    EXPECT_TRUE(has_bat) << "Should have 'bat1:bat' device";
    EXPECT_TRUE(has_vin) << "Should have 'bat1:vin' device";
    EXPECT_TRUE(has_vout) << "Should have 'bat1:vout' device";

    // Verify connections have prefix
    EXPECT_FALSE(ctx.connections.empty()) << "Should have loaded connections";

    bool has_rewrite_conn = false;
    for (const auto& conn : ctx.connections) {
        if (conn.from == "bat1:vin.port" || conn.to == "bat1:vin.port") {
            has_rewrite_conn = true;
        }
    }
    EXPECT_TRUE(has_rewrite_conn) << "Connections should be rewritten with prefix";
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
    // Create test blueprint
    std::string blueprint_path = "blueprints/direct_test.json";
    create_test_blueprint(blueprint_path);

    // Load blueprint file directly
    std::ifstream file(blueprint_path);
    ASSERT_TRUE(file.is_open()) << "Blueprint file should exist";

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Parse should work
    ParserContext ctx;
    EXPECT_NO_THROW(ctx = parse_json(content));

    // Verify structure
    EXPECT_EQ(ctx.devices.size(), 4);  // gnd, bat, vin, vout
    EXPECT_EQ(ctx.connections.size(), 3);  // 3 connections
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
        {{"name", "bat1"}, {"classname", "simple_battery"}},  // Loads from blueprints/
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
    // Load the simple_battery blueprint
    std::string blueprint_path = "../blueprints/simple_battery.json";

    std::ifstream file(blueprint_path);
    if (!file.is_open()) {
        blueprint_path = "blueprints/simple_battery.json";
        file.open(blueprint_path);
    }
    if (!file.is_open()) {
        blueprint_path = "../../blueprints/simple_battery.json";
        file.open(blueprint_path);
    }
    ASSERT_TRUE(file.is_open()) << "simple_battery.json should exist at ../blueprints/, blueprints/, or ../../blueprints/";

    std::string content((std::istreambuf_iterator<char>(file)),
                        (std::istreambuf_iterator<char>()));

    ParserContext ctx = parse_json(content);

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
    // SimpleBattery is cpp_class=false in library/ and has devices/connections
    TypeRegistry reg = load_type_registry("library/");
    ASSERT_TRUE(reg.has("SimpleBattery"));
    const auto* def = reg.get("SimpleBattery");
    ASSERT_FALSE(def->cpp_class);
    ASSERT_FALSE(def->devices.empty());

    // Use SimpleBattery in a root circuit — it should be expanded from TypeRegistry
    nlohmann::json root;
    root["devices"] = {
        {{"name", "gnd"}, {"classname", "RefNode"}, {"params", {{"value", "0.0"}}}},
        {{"name", "sb"}, {"classname", "SimpleBattery"}},
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
