#include <gtest/gtest.h>
#include "data/blueprint.h"
#include "json_parser/json_parser.h"
#include <filesystem>
#include <fstream>


// =============================================================================
// Helper: build a minimal TypeRegistry from library/ on disk
// =============================================================================
static TypeRegistry make_registry() {
    return load_type_registry("library");
}

// =============================================================================
// Helper: build a hand-authored TypeDefinition (like simple_battery.json)
// =============================================================================
static TypeDefinition make_simple_blueprint() {
    TypeDefinition def;
    def.classname = "TestBattery";
    def.cpp_class = false;

    def.ports["vin"]  = {PortDirection::In, PortType::V};
    def.ports["vout"] = {PortDirection::Out, PortType::V};

    DeviceInstance gnd;
    gnd.name = "gnd"; gnd.classname = "RefNode";
    gnd.params["value"] = "0.0";

    DeviceInstance bat;
    bat.name = "bat"; bat.classname = "Battery";
    bat.params["v_nominal"] = "28.0";

    DeviceInstance bi;
    bi.name = "vin"; bi.classname = "BlueprintInput";
    bi.params["exposed_type"] = "V";
    bi.params["exposed_direction"] = "In";

    DeviceInstance bo;
    bo.name = "vout"; bo.classname = "BlueprintOutput";
    bo.params["exposed_type"] = "V";
    bo.params["exposed_direction"] = "Out";

    def.devices = {gnd, bat, bi, bo};
    def.connections = {
        {"vin.port", "bat.v_in"},
        {"bat.v_out", "vout.port"},
        {"gnd.v", "vin.port"},
    };

    return def;
}

// =============================================================================
// Expand empty TypeDefinition → empty Blueprint
// =============================================================================
TEST(ExpandTypeDef, EmptyDef_ProducesEmptyBlueprint) {
    TypeDefinition def;
    def.classname = "Empty";
    def.cpp_class = false;
    TypeRegistry reg;

    Blueprint bp = expand_type_definition(def, reg);

    EXPECT_EQ(bp.nodes.size(), 0);
    EXPECT_EQ(bp.wires.size(), 0);
}

// =============================================================================
// Basic expansion: correct number of nodes and wires
// =============================================================================
TEST(ExpandTypeDef, SimpleBattery_NodeAndWireCount) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    EXPECT_EQ(bp.nodes.size(), 4);  // gnd, bat, vin, vout
    EXPECT_EQ(bp.wires.size(), 3);  // 3 connections
}

// =============================================================================
// Node IDs match device names
// =============================================================================
TEST(ExpandTypeDef, NodeIds_MatchDeviceNames) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    ASSERT_NE(bp.find_node("gnd"), nullptr);
    ASSERT_NE(bp.find_node("bat"), nullptr);
    ASSERT_NE(bp.find_node("vin"), nullptr);
    ASSERT_NE(bp.find_node("vout"), nullptr);
}

// =============================================================================
// Ports are populated from registry when device has no inline ports
// =============================================================================
TEST(ExpandTypeDef, PortsFallbackToRegistry) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    // Battery should have ports from registry (v_in, v_out at minimum)
    const Node* bat = bp.find_node("bat");
    ASSERT_NE(bat, nullptr);
    EXPECT_FALSE(bat->inputs.empty());
    EXPECT_FALSE(bat->outputs.empty());

    // Check specific port names
    bool has_v_in = false, has_v_out = false;
    for (const auto& p : bat->inputs) if (p.name == "v_in") has_v_in = true;
    for (const auto& p : bat->outputs) if (p.name == "v_out") has_v_out = true;
    EXPECT_TRUE(has_v_in) << "Battery should have v_in input port";
    EXPECT_TRUE(has_v_out) << "Battery should have v_out output port";
}

// =============================================================================
// Params: registry defaults merged with device overrides
// =============================================================================
TEST(ExpandTypeDef, Params_RegistryDefaultsMergedWithOverrides) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    const Node* bat = bp.find_node("bat");
    ASSERT_NE(bat, nullptr);

    // Override from device
    auto it = bat->params.find("v_nominal");
    ASSERT_NE(it, bat->params.end());
    EXPECT_EQ(it->second, "28.0");

    // Default from registry (Battery has internal_r default)
    const auto* bat_def = reg.get("Battery");
    if (bat_def) {
        auto def_it = bat_def->params.find("internal_r");
        if (def_it != bat_def->params.end()) {
            auto node_it = bat->params.find("internal_r");
            EXPECT_NE(node_it, bat->params.end()) << "Registry default param should be present";
        }
    }
}

// =============================================================================
// render_hint from registry
// =============================================================================
TEST(ExpandTypeDef, RenderHint_FromRegistry) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    const Node* gnd = bp.find_node("gnd");
    ASSERT_NE(gnd, nullptr);
    EXPECT_EQ(gnd->render_hint, "ref") << "RefNode should have render_hint='ref'";
}

// =============================================================================
// Wire endpoints are correctly parsed from "device.port" format
// =============================================================================
TEST(ExpandTypeDef, WireEndpoints_Correct) {
    auto def = make_simple_blueprint();
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    // Find wire "vin.port" -> "bat.v_in"
    bool found = false;
    for (const auto& w : bp.wires) {
        if (w.start.node_id == "vin" && w.start.port_name == "port" &&
            w.end.node_id == "bat" && w.end.port_name == "v_in") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Should have wire from vin.port to bat.v_in";
}

// =============================================================================
// Device positions preserved when available
// =============================================================================
TEST(ExpandTypeDef, Positions_PreservedFromDevices) {
    TypeDefinition def;
    def.classname = "PosTest";
    def.cpp_class = false;

    DeviceInstance d1;
    d1.name = "a"; d1.classname = "Battery";
    d1.pos = {100.0f, 200.0f};
    d1.size = {128.0f, 96.0f};

    DeviceInstance d2;
    d2.name = "b"; d2.classname = "Resistor";
    d2.pos = {300.0f, 400.0f};

    def.devices = {d1, d2};
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    const Node* a = bp.find_node("a");
    ASSERT_NE(a, nullptr);
    EXPECT_FLOAT_EQ(a->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(a->pos.y, 200.0f);
    EXPECT_FLOAT_EQ(a->size.x, 128.0f);
    EXPECT_FLOAT_EQ(a->size.y, 96.0f);

    const Node* b = bp.find_node("b");
    ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(b->pos.x, 300.0f);
    EXPECT_FLOAT_EQ(b->pos.y, 400.0f);
}

// =============================================================================
// Default size from registry when device has no size
// =============================================================================
TEST(ExpandTypeDef, Size_FallbackToRegistry) {
    TypeDefinition def;
    def.classname = "SizeTest";
    def.cpp_class = false;

    DeviceInstance d1;
    d1.name = "bat"; d1.classname = "Battery";
    // No size set — should use registry default

    def.devices = {d1};
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    const Node* bat = bp.find_node("bat");
    ASSERT_NE(bat, nullptr);
    // Should have a non-zero size from registry or default
    EXPECT_GT(bat->size.x, 0.0f);
    EXPECT_GT(bat->size.y, 0.0f);
}

// =============================================================================
// Routing points preserved on connections
// =============================================================================
TEST(ExpandTypeDef, RoutingPoints_Preserved) {
    TypeDefinition def;
    def.classname = "RouteTest";
    def.cpp_class = false;

    DeviceInstance d1; d1.name = "a"; d1.classname = "Battery";
    DeviceInstance d2; d2.name = "b"; d2.classname = "Resistor";
    def.devices = {d1, d2};

    Connection conn;
    conn.from = "a.v_out";
    conn.to = "b.v_in";
    conn.routing_points = {{150.0f, 250.0f}, {200.0f, 300.0f}};
    def.connections = {conn};

    auto reg = make_registry();
    Blueprint bp = expand_type_definition(def, reg);

    ASSERT_EQ(bp.wires.size(), 1);
    ASSERT_EQ(bp.wires[0].routing_points.size(), 2);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].x, 150.0f);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].y, 250.0f);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[1].x, 200.0f);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[1].y, 300.0f);
}

// =============================================================================
// parse_type_definition reads "wires" as connections with routing_points
// =============================================================================
TEST(ExpandTypeDef, ParseTypeDefinition_WiresFormat) {
    // Simulate a v2 blueprint that has nodes and wires with routing points
    std::string json_str = R"({
        "version": 2,
        "meta": {
            "name": "WireTest",
            "cpp_class": false
        },
        "exposes": {},
        "nodes": {
            "a": {"type": "Battery", "pos": [0, 0]},
            "b": {"type": "Resistor", "pos": [0, 0]}
        },
        "wires": [
            {
                "id": "w1",
                "from": ["a", "v_out"],
                "to": ["b", "v_in"],
                "routing": [[100.0, 200.0]]
            }
        ]
    })";

    // Write to temp file as .blueprint, load via type registry
    auto tmp = std::filesystem::temp_directory_path() / "expand_test_lib";
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "WireTest.blueprint");
        f << json_str;
    }

    auto reg = load_type_registry(tmp.string());
    const auto* def = reg.get("WireTest");
    ASSERT_NE(def, nullptr);
    ASSERT_EQ(def->connections.size(), 1);

    // Verify routing points were parsed
    EXPECT_EQ(def->connections[0].from, "a.v_out");
    EXPECT_EQ(def->connections[0].to, "b.v_in");
    ASSERT_EQ(def->connections[0].routing_points.size(), 1);
    EXPECT_FLOAT_EQ(def->connections[0].routing_points[0].first, 100.0f);
    EXPECT_FLOAT_EQ(def->connections[0].routing_points[0].second, 200.0f);

    // Expand and verify routing points survive into Blueprint
    Blueprint bp = expand_type_definition(*def, reg);
    ASSERT_EQ(bp.wires.size(), 1);
    ASSERT_EQ(bp.wires[0].routing_points.size(), 1);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].x, 100.0f);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].y, 200.0f);

    // Cleanup
    std::filesystem::remove_all(tmp);
}

// =============================================================================
// parse_type_definition reads device pos/size from JSON
// =============================================================================
TEST(ExpandTypeDef, ParseTypeDefinition_DevicePosSize) {
    std::string json_str = R"({
        "version": 2,
        "meta": {
            "name": "LayoutTest",
            "cpp_class": false
        },
        "exposes": {},
        "nodes": {
            "bat": {
                "type": "Battery",
                "pos": [96.0, 112.0],
                "size": [128.0, 80.0]
            }
        },
        "wires": []
    })";

    auto tmp = std::filesystem::temp_directory_path() / "expand_test_lib2";
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "LayoutTest.blueprint");
        f << json_str;
    }

    auto reg = load_type_registry(tmp.string());
    const auto* def = reg.get("LayoutTest");
    ASSERT_NE(def, nullptr);
    ASSERT_EQ(def->devices.size(), 1);

    // Verify pos/size parsed into DeviceInstance
    ASSERT_TRUE(def->devices[0].pos.has_value());
    EXPECT_FLOAT_EQ(def->devices[0].pos->first, 96.0f);
    EXPECT_FLOAT_EQ(def->devices[0].pos->second, 112.0f);
    ASSERT_TRUE(def->devices[0].size.has_value());
    EXPECT_FLOAT_EQ(def->devices[0].size->first, 128.0f);
    EXPECT_FLOAT_EQ(def->devices[0].size->second, 80.0f);

    // Expand and verify positions survive into Blueprint
    Blueprint bp = expand_type_definition(*def, reg);
    ASSERT_EQ(bp.nodes.size(), 1);
    EXPECT_FLOAT_EQ(bp.nodes[0].pos.x, 96.0f);
    EXPECT_FLOAT_EQ(bp.nodes[0].pos.y, 112.0f);
    EXPECT_FLOAT_EQ(bp.nodes[0].size.x, 128.0f);
    EXPECT_FLOAT_EQ(bp.nodes[0].size.y, 80.0f);

    std::filesystem::remove_all(tmp);
}

// =============================================================================
// Inline ports on devices preferred over registry ports
// =============================================================================
TEST(ExpandTypeDef, InlinePorts_PreferredOverRegistry) {
    TypeDefinition def;
    def.classname = "InlinePortTest";
    def.cpp_class = false;

    DeviceInstance d1;
    d1.name = "custom"; d1.classname = "Battery";
    // Provide inline ports that differ from registry
    d1.ports["custom_in"] = {PortDirection::In, PortType::V};
    d1.ports["custom_out"] = {PortDirection::Out, PortType::V};

    def.devices = {d1};
    auto reg = make_registry();

    Blueprint bp = expand_type_definition(def, reg);

    const Node* n = bp.find_node("custom");
    ASSERT_NE(n, nullptr);

    bool has_custom_in = false, has_custom_out = false;
    for (const auto& p : n->inputs) if (p.name == "custom_in") has_custom_in = true;
    for (const auto& p : n->outputs) if (p.name == "custom_out") has_custom_out = true;

    EXPECT_TRUE(has_custom_in) << "Should use inline port custom_in, not registry ports";
    EXPECT_TRUE(has_custom_out) << "Should use inline port custom_out, not registry ports";
}

// =============================================================================
// Real library blueprint (simple_battery) round-trips through expand
// =============================================================================
TEST(ExpandTypeDef, RealLibrary_SimpleBattery) {
    auto reg = make_registry();
    const auto* def = reg.get("simple_battery");
    if (!def) GTEST_SKIP() << "simple_battery not found in library/";

    EXPECT_FALSE(def->cpp_class);
    EXPECT_FALSE(def->devices.empty());
    EXPECT_FALSE(def->connections.empty());

    Blueprint bp = expand_type_definition(*def, reg);

    EXPECT_EQ(bp.nodes.size(), def->devices.size());
    EXPECT_EQ(bp.wires.size(), def->connections.size());

    // Every node should have a non-empty type_name
    for (const auto& n : bp.nodes) {
        EXPECT_FALSE(n.type_name.empty()) << "Node " << n.id << " has empty type_name";
    }
}
