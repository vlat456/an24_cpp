#include <gtest/gtest.h>
#include "editor/render.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport/viewport.h"
#include "editor/simulation.h"
#include "editor/persist.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
using namespace an24;

// Helper to get signal voltage by port name
static float get_voltage(const SimulationState& state, const BuildResult& result,
                          const std::string& port_name) {
    auto it = result.port_to_signal.find(port_name);
    EXPECT_NE(it, result.port_to_signal.end()) << "Port not found: " << port_name;
    if (it == result.port_to_signal.end()) return 0.0f;
    return state.across[it->second];
}

/// TDD Phase 5: Editor Hierarchical Blueprint Support
/// Tests for collapsed node rendering, navigation, and persistence

// =============================================================================
// Test: Collapsed Node Rendering
// =============================================================================

TEST(CollapsedNode, RendersAsSingleNode) {
    // FAIL: Node struct doesn't have 'collapsed' field yet
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "bat1";
    n.type_name = "simple_battery";  // Nested blueprint
    n.collapsed = true;
    n.pos = Pt(100.0f, 50.0f);
    n.size = Pt(120.0f, 80.0f);

    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Should render single rectangle for collapsed node
    EXPECT_TRUE(dl.had_rect()) << "Collapsed node should render as rectangle";
}

TEST(CollapsedNode, ExposedPorts_RenderOnBoundary) {
    // FAIL: Can't set exposed ports without blueprint_path field
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "bat1";
    n.type_name = "simple_battery";
    n.collapsed = true;
    n.blueprint_path = "blueprints/simple_battery.json";  // FAIL: No such field

    // Exposed ports from blueprint
    n.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    n.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));

    n.pos = Pt(100.0f, 50.0f);
    n.size = Pt(120.0f, 80.0f);

    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Should render circles for ports on node boundary
    EXPECT_TRUE(dl.had_circle()) << "Exposed ports should render as circles";

    // Should have at least 2 ports (vin input, vout output)
    EXPECT_GE(dl.circle_colors_.size(), 2u) << "Should render at least 2 exposed ports";
}

TEST(CollapsedNode, PortColors_MatchExposedType) {
    // FAIL: Need port type color rendering
    Blueprint bp;
    Node n;
    n.id = "dev1";
    n.name = "dev1";
    n.type_name = "multi_port_device";
    n.collapsed = true;

    // Different port types for color testing
    n.inputs.push_back(::Port("v_in", PortSide::Input, an24::PortType::V));      // Red
    n.inputs.push_back(::Port("i_in", PortSide::Input, an24::PortType::I));      // Blue
    n.inputs.push_back(::Port("bool_in", PortSide::Input, an24::PortType::Bool)); // Green
    n.outputs.push_back(::Port("rpm_out", PortSide::Output, an24::PortType::RPM)); // Orange

    n.pos = Pt(100.0f, 50.0f);
    n.size = Pt(140.0f, 80.0f);

    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Check for specific port type colors
    // Format: 0xAABBGGRR (alpha, blue, green, red) - see get_port_color()
    EXPECT_TRUE(dl.has_circle_with_color(0xFF0000FF)) << "V port should be red (0xFF0000FF = AABBGGRR)";
    EXPECT_TRUE(dl.has_circle_with_color(0xFFFF0000)) << "I port should be blue (0xFFFF0000 = AABBGGRR)";
    EXPECT_TRUE(dl.has_circle_with_color(0xFF00FF00)) << "Bool port should be green (0xFF00FF00 = AABBGGRR)";
    EXPECT_TRUE(dl.has_circle_with_color(0xFF00A5FF)) << "RPM port should be orange (0xFF00A5FF = AABBGGRR)";
}

TEST(CollapsedNode, VisualIndicator_IconOrBadge) {
    // FAIL: Need visual indicator for collapsible nodes
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "bat1";
    n.type_name = "simple_battery";
    n.collapsed = true;
    n.pos = Pt(100.0f, 50.0f);
    n.size = Pt(120.0f, 80.0f);

    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Should have some visual indicator (text, small rect, etc.)
    // For now, just check that something was rendered
    EXPECT_TRUE(dl.had_rect() || !dl.texts_.empty())
        << "Collapsed node should have visual indicator";
}

// =============================================================================
// Test: Exposed Ports from Blueprint
// =============================================================================

TEST(ExposedPorts, LoadFromBlueprint_ExtractExposedPorts) {
    // FAIL: Need to integrate extract_exposed_ports with editor
    // This test verifies we can load blueprint and extract exposed ports

    std::string blueprint_path = "blueprints/simple_battery.json";

    // Try to find blueprint (same path resolution as parse_json)
    std::filesystem::path fs_path(blueprint_path);
    if (!std::filesystem::exists(fs_path) && fs_path.is_relative()) {
        std::vector<std::filesystem::path> try_paths = {
            fs_path,
            "../" / fs_path,
            "../../" / fs_path,
        };
        for (const auto& path : try_paths) {
            if (std::filesystem::exists(path)) {
                fs_path = path;
                break;
            }
        }
    }

    ASSERT_TRUE(std::filesystem::exists(fs_path))
        << "Test blueprint should exist at: " << blueprint_path;

    // Load blueprint
    std::ifstream file(fs_path);
    ASSERT_TRUE(file.is_open()) << "Blueprint file should be readable";

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    ParserContext ctx = parse_json(content);
    auto exposed_ports = extract_exposed_ports(ctx);

    // Should have 2 exposed ports: vin (In), vout (Out)
    EXPECT_EQ(exposed_ports.size(), 2u) << "simple_battery should have 2 exposed ports";

    EXPECT_TRUE(exposed_ports.count("vin")) << "Should have 'vin' exposed port";
    EXPECT_EQ(exposed_ports["vin"].direction, an24::PortDirection::In)
        << "vin should be Input (data flows into blueprint)";
    EXPECT_EQ(exposed_ports["vin"].type, an24::PortType::V)
        << "vin should be Voltage type";

    EXPECT_TRUE(exposed_ports.count("vout")) << "Should have 'vout' exposed port";
    EXPECT_EQ(exposed_ports["vout"].direction, an24::PortDirection::Out)
        << "vout should be Output (data flows out of blueprint)";
    EXPECT_EQ(exposed_ports["vout"].type, an24::PortType::V)
        << "vout should be Voltage type";
}

TEST(ExposedPorts, PopulateNodePorts_FromExposedPorts) {
    // FAIL: Need to convert exposed ports to Node::inputs/outputs
    // This test verifies the conversion logic

    // Simulate loading nested blueprint
    std::string classname = "simple_battery";
    std::string blueprint_path = "blueprints/" + classname + ".json";

    // Find blueprint
    std::filesystem::path fs_path(blueprint_path);
    if (!std::filesystem::exists(fs_path) && fs_path.is_relative()) {
        std::vector<std::filesystem::path> try_paths = {
            fs_path,
            "../" / fs_path,
            "../../" / fs_path,
        };
        for (const auto& path : try_paths) {
            if (std::filesystem::exists(path)) {
                fs_path = path;
                break;
            }
        }
    }

    ASSERT_TRUE(std::filesystem::exists(fs_path)) << "Test blueprint should exist";

    // Load and extract
    std::ifstream file(fs_path);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    ParserContext nested_ctx = parse_json(content);
    auto exposed_ports = extract_exposed_ports(nested_ctx);

    // Create node from exposed ports
    Node node;
    node.id = "bat1";
    node.name = "bat1";
    node.type_name = classname;
    node.collapsed = true;

    // Populate inputs/outputs from exposed_ports
    for (const auto& [name, port] : exposed_ports) {
        if (port.direction == PortDirection::In) {
            node.inputs.push_back(::Port(name.c_str(), PortSide::Input, port.type));
        } else {
            node.outputs.push_back(::Port(name.c_str(), PortSide::Output, port.type));
        }
    }
    EXPECT_EQ(node.inputs.size(), 1u) << "Should have 1 input port (vin)";
    EXPECT_EQ(node.outputs.size(), 1u) << "Should have 1 output port (vout)";

    if (!node.inputs.empty()) {
        EXPECT_EQ(node.inputs[0].name, "vin");
        EXPECT_EQ(node.inputs[0].type, an24::PortType::V);
    }

    if (!node.outputs.empty()) {
        EXPECT_EQ(node.outputs[0].name, "vout");
        EXPECT_EQ(node.outputs[0].type, an24::PortType::V);
    }
}

// =============================================================================
// Test: Expanded vs Collapsed Visual Distinction
// =============================================================================

TEST(CollapsedNode, VisualStyle_DifferentFromRegularNode) {
    // FAIL: Need different visual style for collapsed nodes
    Blueprint bp;

    // Regular component node
    Node regular;
    regular.id = "battery";
    regular.name = "Battery";
    regular.type_name = "Battery";
    regular.collapsed = false;
    regular.pos = Pt(50.0f, 50.0f);
    regular.size = Pt(120.0f, 80.0f);
    bp.add_node(std::move(regular));

    // Collapsed nested blueprint node
    Node collapsed;
    collapsed.id = "bat1";
    collapsed.name = "bat1";
    collapsed.type_name = "simple_battery";
    collapsed.collapsed = true;
    collapsed.pos = Pt(200.0f, 50.0f);
    collapsed.size = Pt(120.0f, 80.0f);
    bp.add_node(std::move(collapsed));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Should have rendered something (implementation details TBD)
    EXPECT_TRUE(dl.had_rect()) << "Should render nodes";
    // Future: check for different colors, styles, or indicators
}

// =============================================================================
// Test: Drill-down Navigation (Phase 5.2 - future)
// =============================================================================

TEST(DrillDownNavigation, DoubleClick_ExpandsNestedBlueprint) {
    // FAIL: Need navigation stack and drill-down logic
    // This is for Phase 5.2, keeping as placeholder

    // TODO: Implement navigation state
    // TODO: Handle double-click event
    // TODO: Load nested blueprint
    // TODO: Push to navigation stack
    // TODO: Render nested blueprint internal devices

    SUCCEED() << "Placeholder for drill-down navigation tests";
}

TEST(DrillDownNavigation, BackButton_ReturnsToParent) {
    // FAIL: Need back button navigation
    // This is for Phase 5.2, keeping as placeholder

    // TODO: Pop navigation stack
    // TODO: Restore parent blueprint
    // TODO: Restore viewport state

    SUCCEED() << "Placeholder for back navigation tests";
}

// =============================================================================
// Test: Persistence (Phase 5.3 - future)
// =============================================================================

TEST(Persistence, CollapsedState_SavesToJson) {
    // Test that collapsed blueprint nodes are saved with correct metadata
    Blueprint bp;

    // Add collapsed lamp_pass_through node
    Node lamp_bp;
    lamp_bp.id = "lamp1";
    lamp_bp.name = "lamp1";
    lamp_bp.type_name = "lamp_pass_through";
    lamp_bp.kind = NodeKind::Blueprint;
    lamp_bp.collapsed = true;
    lamp_bp.blueprint_path = "blueprints/lamp_pass_through.json";
    lamp_bp.pos = Pt(200.0f, 100.0f);
    lamp_bp.size = Pt(120.0f, 80.0f);
    lamp_bp.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    lamp_bp.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(lamp_bp));

    // Convert to JSON
    std::string json_str = blueprint_to_json(bp);

    // Parse JSON to verify
    json j = json::parse(json_str);

    ASSERT_TRUE(j.contains("devices")) << "JSON should have devices array";
    auto devices = j["devices"];
    ASSERT_EQ(devices.size(), 1u) << "Should have 1 device";

    auto& lamp_device = devices[0];
    EXPECT_EQ(lamp_device["name"], "lamp1");
    EXPECT_EQ(lamp_device["classname"], "lamp_pass_through");
    EXPECT_EQ(lamp_device["kind"], "Blueprint") << "Kind should be Blueprint, not Node";
    EXPECT_TRUE(lamp_device.contains("blueprint_path")) << "Should have blueprint_path field";
    EXPECT_EQ(lamp_device["blueprint_path"], "blueprints/lamp_pass_through.json");
}

TEST(Persistence, CollapsedState_LoadsFromJson) {
    // Test that collapsed blueprint nodes are loaded correctly from JSON
    const char* json_str = R"(
    {
      "devices": [
        {
          "name": "gnd",
          "classname": "RefNode",
          "kind": "Ref",
          "params": {"value": "0.0"}
        },
        {
          "name": "lamp1",
          "classname": "lamp_pass_through",
          "kind": "Blueprint",
          "blueprint_path": "blueprints/lamp_pass_through.json"
        }
      ],
      "connections": [
        {
          "from": "gnd.v",
          "to": "lamp1.vin"
        }
      ]
    }
    )";

    // Load JSON
    auto bp_result = blueprint_from_json(json_str);
    ASSERT_TRUE(bp_result.has_value()) << "Should parse blueprint successfully";

    Blueprint& bp = *bp_result;

    // Should have 2 nodes
    EXPECT_EQ(bp.nodes.size(), 2u) << "Should have 2 nodes (gnd + collapsed lamp1)";

    // Find the lamp1 node
    Node* lamp_node = nullptr;
    for (const auto& n : bp.nodes) {
        if (n.id == "lamp1") {
            lamp_node = const_cast<Node*>(&n);
            break;
        }
    }

    ASSERT_NE(lamp_node, nullptr) << "Should find lamp1 node";

    // Verify metadata is preserved
    EXPECT_EQ(lamp_node->id, "lamp1");
    EXPECT_EQ(lamp_node->type_name, "lamp_pass_through");
    EXPECT_EQ(lamp_node->kind, NodeKind::Blueprint) << "Kind should be Blueprint";
    EXPECT_TRUE(lamp_node->collapsed) << "Should be marked as collapsed";
    EXPECT_EQ(lamp_node->blueprint_path, "blueprints/lamp_pass_through.json") << "blueprint_path should be preserved";
}

TEST(Persistence, RoundTrip_SaveLoad_PreservesCollapsedBlueprint) {
    // Create blueprint with collapsed lamp_pass_through node
    Blueprint original;

    // Voltage source (28V RefNode)
    Node source;
    source.id = "source";
    source.name = "source";
    source.type_name = "RefNode";
    source.kind = NodeKind::Ref;
    source.pos = Pt(50.0f, 100.0f);
    source.size = Pt(80.0f, 60.0f);
    source.params["value"] = "28.0";
    source.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    original.add_node(std::move(source));

    // Collapsed lamp_pass_through
    Node lamp_bp;
    lamp_bp.id = "lamp1";
    lamp_bp.name = "lamp1";
    lamp_bp.type_name = "lamp_pass_through";
    lamp_bp.kind = NodeKind::Blueprint;
    lamp_bp.collapsed = true;
    lamp_bp.blueprint_path = "blueprints/lamp_pass_through.json";
    lamp_bp.pos = Pt(200.0f, 100.0f);
    lamp_bp.size = Pt(120.0f, 80.0f);
    lamp_bp.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    lamp_bp.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    original.add_node(std::move(lamp_bp));

    // Wire: source → lamp_bp input
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("source", "v", PortSide::Output);
    wire1.end = WireEnd("lamp1", "vin", PortSide::Input);
    original.add_wire(wire1);

    // Save to JSON
    std::string json_str = blueprint_to_json(original);

    // Load from JSON
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value()) << "Failed to load blueprint from JSON";

    // Verify collapsed blueprint metadata preserved
    EXPECT_EQ(loaded->nodes.size(), 2) << "Should have 2 nodes";

    auto lamp_node = std::find_if(loaded->nodes.begin(), loaded->nodes.end(),
        [](const Node& n) { return n.id == "lamp1"; });
    ASSERT_NE(lamp_node, loaded->nodes.end()) << "lamp1 node should exist";

    EXPECT_EQ(lamp_node->type_name, "lamp_pass_through");
    EXPECT_EQ(lamp_node->kind, NodeKind::Blueprint) << "Kind should be Blueprint";
    EXPECT_TRUE(lamp_node->collapsed) << "Should be marked as collapsed";
    EXPECT_EQ(lamp_node->blueprint_path, "blueprints/lamp_pass_through.json") << "blueprint_path should be preserved";

    // Verify simulation still works after round-trip
    SimulationController sim;
    sim.build(*loaded);

    ASSERT_TRUE(sim.build_result.has_value()) << "Failed to build simulation from loaded blueprint";
    auto& result = *sim.build_result;

    // Run simulation
    sim.start();
    for (int step = 0; step < 100; ++step) {
        sim.step(sim.dt);
    }
    sim.stop();

    // Check voltage flows through the collapsed blueprint
    auto lamp_vout_it = result.port_to_signal.find("lamp1:vout.port");
    ASSERT_NE(lamp_vout_it, result.port_to_signal.end()) << "lamp1:vout.port should exist in simulation";

    float lamp_vout = sim.state.across[lamp_vout_it->second];
    EXPECT_GT(lamp_vout, 20.0f) << "Blueprint output should be >20V after round-trip save/load";
}

// =============================================================================
// Integration Tests: Voltage Flow Through Collapsed Blueprints
// =============================================================================

TEST(VoltageFlow, CollapsedBlueprint_PassesVoltage) {
    // Create blueprint with collapsed lamp_pass_through node
    Blueprint bp;

    // Voltage source (28V RefNode)
    Node source;
    source.id = "source";
    source.name = "source";
    source.type_name = "RefNode";
    source.kind = NodeKind::Ref;
    source.pos = Pt(50.0f, 100.0f);
    source.size = Pt(80.0f, 60.0f);
    source.params["value"] = "28.0";
    source.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(source));

    // Collapsed lamp_pass_through
    Node lamp_bp;
    lamp_bp.id = "lamp1";
    lamp_bp.name = "lamp1";
    lamp_bp.type_name = "lamp_pass_through";
    lamp_bp.kind = NodeKind::Blueprint;
    lamp_bp.collapsed = true;
    lamp_bp.blueprint_path = "blueprints/lamp_pass_through.json";
    lamp_bp.pos = Pt(200.0f, 100.0f);
    lamp_bp.size = Pt(120.0f, 80.0f);
    // Exposed ports from lamp_pass_through: vin (In), vout (Out)
    lamp_bp.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    lamp_bp.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(lamp_bp));

    // Wire: source → lamp_bp input
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("source", "v", PortSide::Output);
    wire1.end = WireEnd("lamp1", "vin", PortSide::Input);
    bp.add_wire(wire1);

    // Build simulation (this will expand the collapsed blueprint)
    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value()) << "Build should succeed";
    auto& result = *sim.build_result;

    // Run simulation for 50 steps to settle
    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(sim.dt);
    }
    sim.stop();

    // Check voltages
    auto source_v = get_voltage(sim.state, result, "source.v");
    auto lamp1_vin = get_voltage(sim.state, result, "lamp1:vin.port");
    auto lamp1_vout = get_voltage(sim.state, result, "lamp1:vout.port");

    EXPECT_NEAR(source_v, 28.0f, 0.1f) << "Source should be at 28V";
    EXPECT_NEAR(lamp1_vin, 28.0f, 0.1f) << "Lamp input should be ~28V";
    EXPECT_GT(lamp1_vout, 20.0f) << "Lamp output should be >20V (some voltage drop across lamp)";
    EXPECT_LT(lamp1_vout, 28.0f) << "Lamp output should be <28V (lamp consumes power)";
}

TEST(VoltageFlow, NestedBlueprintWithBattery) {
    // Test that a blueprint containing a Battery actually outputs voltage
    Blueprint bp;

    // Ground reference
    Node gnd;
    gnd.id = "gnd";
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.pos = Pt(50.0f, 200.0f);
    gnd.size = Pt(80.0f, 60.0f);
    gnd.params["value"] = "0.0";
    gnd.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(gnd));

    // Collapsed simple_battery (contains Battery that outputs 28V)
    Node battery_bp;
    battery_bp.id = "bat1";
    battery_bp.name = "bat1";
    battery_bp.type_name = "simple_battery";
    battery_bp.kind = NodeKind::Blueprint;
    battery_bp.collapsed = true;
    battery_bp.blueprint_path = "blueprints/simple_battery.json";
    battery_bp.pos = Pt(200.0f, 100.0f);
    battery_bp.size = Pt(120.0f, 80.0f);
    battery_bp.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    battery_bp.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(battery_bp));

    // Wire: ground → battery input
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("gnd", "v", PortSide::Output);
    wire1.end = WireEnd("bat1", "vin", PortSide::Input);
    bp.add_wire(wire1);

    // Build simulation
    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value()) << "Build should succeed";
    auto& result = *sim.build_result;

    // Run simulation
    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(sim.dt);
    }
    sim.stop();

    // Check battery output voltage
    float bat1_vout = get_voltage(sim.state, result, "bat1:vout.port");
    EXPECT_GT(bat1_vout, 25.0f) << "Simple battery should output ~28V";
    EXPECT_LT(bat1_vout, 30.0f) << "Simple battery should not exceed nominal significantly";

    float gnd_v = get_voltage(sim.state, result, "gnd.v");
    EXPECT_NEAR(gnd_v, 0.0f, 0.1f) << "Ground should be at 0V";
}

// Regression: non-zero RefNode must be marked as fixed signal (was only marking value="0.0")
TEST(VoltageFlow, RefNode_NonZeroValue_IsFixedSignal) {
    Blueprint bp;

    // 28V reference
    Node ref28;
    ref28.id = "ref28";
    ref28.name = "ref28";
    ref28.type_name = "RefNode";
    ref28.kind = NodeKind::Ref;
    ref28.pos = Pt(50.0f, 100.0f);
    ref28.size = Pt(80.0f, 60.0f);
    ref28.params["value"] = "28.0";
    ref28.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(ref28));

    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value());
    auto& result = *sim.build_result;

    // The RefNode's signal must be in the fixed_signals list
    auto it = result.port_to_signal.find("ref28.v");
    ASSERT_NE(it, result.port_to_signal.end()) << "ref28.v must be in port_to_signal";
    uint32_t sig = it->second;
    bool is_fixed = std::binary_search(
        result.fixed_signals.begin(), result.fixed_signals.end(), sig);
    EXPECT_TRUE(is_fixed) << "Non-zero RefNode signal must be marked fixed";

    // Voltage must hold at 28V after simulation steps
    sim.start();
    for (int step = 0; step < 100; ++step) {
        sim.step(sim.dt);
    }
    sim.stop();

    float v = get_voltage(sim.state, result, "ref28.v");
    EXPECT_NEAR(v, 28.0f, 0.01f) << "Fixed 28V RefNode must hold its value";
}

// Regression: multiple RefNodes with different values all get fixed signals
TEST(VoltageFlow, MultipleRefNodes_AllFixed) {
    Blueprint bp;

    auto add_ref = [&](const std::string& id, const std::string& value) {
        Node n;
        n.id = id;
        n.name = id;
        n.type_name = "RefNode";
        n.kind = NodeKind::Ref;
        n.pos = Pt(50.0f, 100.0f);
        n.size = Pt(80.0f, 60.0f);
        n.params["value"] = value;
        n.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
        bp.add_node(std::move(n));
    };

    add_ref("gnd", "0.0");
    add_ref("v12", "12.0");
    add_ref("v28", "28.0");
    add_ref("v115", "115.0");

    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value());
    auto& result = *sim.build_result;

    // All 4 RefNodes must produce fixed signals
    EXPECT_GE(result.fixed_signals.size(), 4u)
        << "All RefNodes must be marked as fixed signals";

    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(sim.dt);
    }
    sim.stop();

    EXPECT_NEAR(get_voltage(sim.state, result, "gnd.v"),  0.0f,   0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v12.v"),  12.0f,  0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v28.v"),  28.0f,  0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v115.v"), 115.0f, 0.01f);
}

// =============================================================================
// Visibility-Based Blueprint Collapsing Tests
// =============================================================================

TEST(Visibility, CollapsedNodeVisible_InternalsHidden_AfterInsertion) {
    // This test simulates what app.cpp::add_blueprint() does
    Blueprint bp;

    // Create collapsed Blueprint node (what add_blueprint creates)
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.name = "lamp1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.kind = NodeKind::Blueprint;
    collapsed.visible = true;  // Collapsed node is VISIBLE
    collapsed.pos = Pt(100.0f, 100.0f);
    collapsed.size = Pt(120.0f, 80.0f);
    collapsed.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    collapsed.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(collapsed));

    // Create internal expanded devices (these would be added by expanding the blueprint)
    auto add_internal = [&](const std::string& id, const std::string& type) {
        Node n;
        n.id = "lamp1:" + id;
        n.name = n.id;
        n.type_name = type;
        n.kind = NodeKind::Node;
        n.visible = false;  // Internal nodes are HIDDEN in parent view
        n.pos = Pt(100.0f, 100.0f);
        n.size = Pt(80.0f, 60.0f);
        bp.add_node(std::move(n));
    };

    add_internal("vin", "BlueprintInput");
    add_internal("lamp", "IndicatorLight");
    add_internal("vout", "BlueprintOutput");

    // Verify visibility state
    EXPECT_EQ(bp.nodes.size(), 4) << "Should have 4 nodes (collapsed + 3 internals)";

    // Count visible vs hidden nodes
    int visible_count = 0;
    int hidden_count = 0;
    for (const auto& n : bp.nodes) {
        if (n.visible) visible_count++;
        else hidden_count++;
    }

    EXPECT_EQ(visible_count, 1) << "Only collapsed node should be visible";
    EXPECT_EQ(hidden_count, 3) << "All 3 internal nodes should be hidden";

    // Verify collapsed node is visible
    Node* collapsed_node = bp.find_node("lamp1");
    ASSERT_NE(collapsed_node, nullptr) << "Collapsed node should exist";
    EXPECT_TRUE(collapsed_node->visible) << "Collapsed node should be visible";
    EXPECT_EQ(collapsed_node->kind, NodeKind::Blueprint) << "Collapsed node should have Blueprint kind";

    // Verify internal nodes are hidden
    for (const auto& internal_id : {"lamp1:vin", "lamp1:lamp", "lamp1:vout"}) {
        Node* internal = bp.find_node(internal_id);
        ASSERT_NE(internal, nullptr) << "Internal node " << internal_id << " should exist";
        EXPECT_FALSE(internal->visible) << "Internal node " << internal_id << " should be hidden";
    }
}

TEST(Visibility, DrillDown_HidesCollapsed_ShowsInternals) {
    // Simulate drill-down operation
    Blueprint bp;

    // Create collapsed node
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.visible = true;
    collapsed.kind = NodeKind::Blueprint;
    bp.add_node(std::move(collapsed));

    // Create internal nodes
    std::vector<std::string> internal_ids;
    auto add_internal = [&](const std::string& id) {
        Node n;
        n.id = "lamp1:" + id;
        n.visible = false;  // Hidden initially
        internal_ids.push_back(n.id);
        bp.add_node(std::move(n));
    };

    add_internal("vin");
    add_internal("lamp");
    add_internal("vout");

    // Simulate drill_into("lamp1")
    Node* collapsed_node = bp.find_node("lamp1");
    ASSERT_NE(collapsed_node, nullptr);
    collapsed_node->visible = false;  // Hide collapsed node

    for (const auto& id : internal_ids) {
        Node* internal = bp.find_node(id);
        ASSERT_NE(internal, nullptr);
        internal->visible = true;  // Show internal nodes
    }

    // Verify drill-down state
    EXPECT_FALSE(collapsed_node->visible) << "Collapsed node should be hidden after drill-down";

    int visible_count = 0;
    for (const auto& n : bp.nodes) {
        if (n.visible) visible_count++;
    }

    EXPECT_EQ(visible_count, 3) << "All 3 internal nodes should be visible after drill-down";

    // Verify all internal nodes are now visible
    for (const auto& id : internal_ids) {
        Node* internal = bp.find_node(id);
        EXPECT_TRUE(internal->visible) << "Internal node " << id << " should be visible after drill-down";
    }
}

TEST(Visibility, DrillOut_ShowsCollapsed_HidesInternals) {
    // Simulate drill-out operation (return to parent view)
    Blueprint bp;

    // Create collapsed node (hidden after drill-down)
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.visible = false;  // Currently hidden (we're drilled in)
    collapsed.kind = NodeKind::Blueprint;
    bp.add_node(std::move(collapsed));

    // Create internal nodes (visible after drill-down)
    std::vector<std::string> internal_ids;
    auto add_internal = [&](const std::string& id) {
        Node n;
        n.id = "lamp1:" + id;
        n.visible = true;  // Currently visible (we're drilled in)
        internal_ids.push_back(n.id);
        bp.add_node(std::move(n));
    };

    add_internal("vin");
    add_internal("lamp");
    add_internal("vout");

    // Simulate drill_out() - return to parent view
    Node* collapsed_node = bp.find_node("lamp1");
    ASSERT_NE(collapsed_node, nullptr);
    collapsed_node->visible = true;  // Show collapsed node

    for (const auto& id : internal_ids) {
        Node* internal = bp.find_node(id);
        ASSERT_NE(internal, nullptr);
        internal->visible = false;  // Hide internal nodes
    }

    // Verify drill-out state (back to collapsed view)
    EXPECT_TRUE(collapsed_node->visible) << "Collapsed node should be visible after drill-out";

    int visible_count = 0;
    for (const auto& n : bp.nodes) {
        if (n.visible) visible_count++;
    }

    EXPECT_EQ(visible_count, 1) << "Only collapsed node should be visible after drill-out";

    // Verify all internal nodes are hidden again
    for (const auto& id : internal_ids) {
        Node* internal = bp.find_node(id);
        EXPECT_FALSE(internal->visible) << "Internal node " << id << " should be hidden after drill-out";
    }
}

TEST(Visibility, Simulation_Works_WithVisibilityToggling) {
    // Test that simulation works correctly regardless of visibility state
    Blueprint bp;

    // Create collapsed Blueprint node
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.visible = true;
    collapsed.kind = NodeKind::Blueprint;
    collapsed.pos = Pt(100.0f, 100.0f);
    collapsed.size = Pt(120.0f, 80.0f);
    collapsed.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    collapsed.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(collapsed));

    // Create internal nodes (expanded from lamp_pass_through blueprint)
    auto add_device = [&](const std::string& id, const std::string& type, bool visible) {
        Node n;
        n.id = "lamp1:" + id;
        n.name = n.id;
        n.type_name = type;
        n.visible = visible;
        n.pos = Pt(100.0f, 100.0f);
        n.size = Pt(80.0f, 60.0f);

        // Add ports based on device type
        if (type == "BlueprintInput") {
            n.outputs.push_back(::Port("port", PortSide::Output, an24::PortType::V));
        } else if (type == "BlueprintOutput") {
            n.inputs.push_back(::Port("port", PortSide::Input, an24::PortType::V));
        } else if (type == "IndicatorLight") {
            n.inputs.push_back(::Port("in", PortSide::Input, an24::PortType::V));
            n.outputs.push_back(::Port("out", PortSide::Output, an24::PortType::V));
        }

        bp.add_node(std::move(n));
    };

    // Add internal devices (hidden in parent view)
    add_device("vin", "BlueprintInput", false);
    add_device("lamp", "IndicatorLight", false);
    add_device("vout", "BlueprintOutput", false);

    // Add internal connections
    Wire conn1;
    conn1.id = "conn1";
    conn1.start = WireEnd("lamp1:vin", "port", PortSide::Output);
    conn1.end = WireEnd("lamp1:lamp", "in", PortSide::Input);
    bp.add_wire(conn1);

    Wire conn2;
    conn2.id = "conn2";
    conn2.start = WireEnd("lamp1:lamp", "out", PortSide::Output);
    conn2.end = WireEnd("lamp1:vout", "port", PortSide::Input);
    bp.add_wire(conn2);

    // Build simulation (should work with ALL nodes regardless of visibility)
    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value()) << "Build should succeed with visibility state";
    auto& result = *sim.build_result;

    // Verify that all 4 devices were created (collapsed + 3 internals)
    EXPECT_EQ(result.devices.size(), 4) << "Simulation should see all 4 nodes (collapsed + 3 internals)";

    // Verify port mapping exists for both collapsed and internal nodes
    // Note: BlueprintInput/BlueprintOutput have .port suffix
    EXPECT_TRUE(result.port_to_signal.count("lamp1:vin.port")) << "BlueprintInput port should be mapped";
    EXPECT_TRUE(result.port_to_signal.count("lamp1:vout.port")) << "BlueprintOutput port should be mapped";
}

// =============================================================================
// Test: Persistence (Phase 5.3)
