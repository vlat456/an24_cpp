#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
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
