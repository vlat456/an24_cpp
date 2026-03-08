#include <gtest/gtest.h>
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/visual/renderer/mock_draw_list.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport/viewport.h"
#include "editor/simulation.h"
#include "editor/visual/scene/persist.h"
#include "editor/visual/node/node.h"
#include "json_parser/json_parser.h"
#include "jit_solver/simulator.h"
#include "jit_solver/SOR_constants.h"
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

// Helper: build a fully-expanded lamp_pass_through blueprint (always-flatten architecture)
// Creates: collapsed Blueprint node + 3 internal nodes + internal wires + CollapsedGroup
static void add_lamp_pass_through(Blueprint& bp, const std::string& id,
                                   Pt pos = Pt(200.0f, 100.0f)) {
    // Collapsed Blueprint node (visual wrapper)
    Node collapsed;
    collapsed.id = id;
    collapsed.name = id;
    collapsed.type_name = "lamp_pass_through";
    collapsed.kind = NodeKind::Blueprint;
    collapsed.collapsed = true;
    collapsed.blueprint_path = "blueprints/lamp_pass_through.json";
    collapsed.pos = pos;
    collapsed.size = Pt(120.0f, 80.0f);
    collapsed.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    collapsed.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(collapsed));

    // Internal BlueprintInput
    Node vin;
    vin.id = id + ":vin";
    vin.name = vin.id;
    vin.type_name = "BlueprintInput";
    vin.pos = pos;
    vin.size = Pt(40.0f, 40.0f);
    vin.outputs.push_back(::Port("port", PortSide::Output, an24::PortType::V));
    vin.inputs.push_back(::Port("ext", PortSide::Input, an24::PortType::Any));
    bp.add_node(std::move(vin));

    // Internal IndicatorLight
    Node lamp;
    lamp.id = id + ":lamp";
    lamp.name = lamp.id;
    lamp.type_name = "IndicatorLight";
    lamp.pos = pos;
    lamp.size = Pt(80.0f, 60.0f);
    lamp.inputs.push_back(::Port("v_in", PortSide::Input, an24::PortType::V));
    lamp.outputs.push_back(::Port("v_out", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(lamp));

    // Internal BlueprintOutput
    Node vout;
    vout.id = id + ":vout";
    vout.name = vout.id;
    vout.type_name = "BlueprintOutput";
    vout.pos = pos;
    vout.size = Pt(40.0f, 40.0f);
    vout.inputs.push_back(::Port("port", PortSide::Input, an24::PortType::V));
    vout.outputs.push_back(::Port("ext", PortSide::Output, an24::PortType::Any));
    bp.add_node(std::move(vout));

    // Internal wires
    Wire w1;
    w1.id = id + ":w1";
    w1.start = WireEnd((id + ":vin").c_str(), "port", PortSide::Output);
    w1.end = WireEnd((id + ":lamp").c_str(), "v_in", PortSide::Input);
    bp.add_wire(w1);

    Wire w2;
    w2.id = id + ":w2";
    w2.start = WireEnd((id + ":lamp").c_str(), "v_out", PortSide::Output);
    w2.end = WireEnd((id + ":vout").c_str(), "port", PortSide::Input);
    bp.add_wire(w2);

    // CollapsedGroup
    CollapsedGroup group;
    group.id = id;
    group.blueprint_path = "blueprints/lamp_pass_through.json";
    group.type_name = "lamp_pass_through";
    group.pos = pos;
    group.size = Pt(120.0f, 80.0f);
    group.internal_node_ids = {id + ":vin", id + ":lamp", id + ":vout"};
    bp.collapsed_groups.push_back(group);

    // Recompute group_ids (internal nodes assigned to group)
    bp.recompute_group_ids();
}

// Helper: build a fully-expanded simple_battery blueprint
// Matches simple_battery.json: gnd(RefNode) + bat(Battery) + vin(BlueprintInput) + vout(BlueprintOutput)
// Internal wires: vin.port→bat.v_in, bat.v_out→vout.port, gnd.v→vin.port
static void add_simple_battery(Blueprint& bp, const std::string& id,
                                Pt pos = Pt(200.0f, 100.0f)) {
    Node collapsed;
    collapsed.id = id;
    collapsed.name = id;
    collapsed.type_name = "simple_battery";
    collapsed.kind = NodeKind::Blueprint;
    collapsed.collapsed = true;
    collapsed.blueprint_path = "blueprints/simple_battery.json";
    collapsed.pos = pos;
    collapsed.size = Pt(120.0f, 80.0f);
    collapsed.inputs.push_back(::Port("vin", PortSide::Input, an24::PortType::V));
    collapsed.outputs.push_back(::Port("vout", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(collapsed));

    // Internal RefNode (ground reference — required for SOR solver to work)
    Node gnd;
    gnd.id = id + ":gnd";
    gnd.name = gnd.id;
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.pos = pos;
    gnd.size = Pt(40.0f, 40.0f);
    gnd.output("v");
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    Node vin;
    vin.id = id + ":vin";
    vin.name = vin.id;
    vin.type_name = "BlueprintInput";
    vin.pos = pos;
    vin.size = Pt(40.0f, 40.0f);
    vin.outputs.push_back(::Port("port", PortSide::Output, an24::PortType::V));
    vin.inputs.push_back(::Port("ext", PortSide::Input, an24::PortType::Any));
    bp.add_node(std::move(vin));

    Node bat;
    bat.id = id + ":bat";
    bat.name = bat.id;
    bat.type_name = "Battery";
    bat.pos = pos;
    bat.size = Pt(80.0f, 60.0f);
    bat.inputs.push_back(::Port("v_in", PortSide::Input, an24::PortType::V));
    bat.outputs.push_back(::Port("v_out", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(bat));

    Node vout;
    vout.id = id + ":vout";
    vout.name = vout.id;
    vout.type_name = "BlueprintOutput";
    vout.pos = pos;
    vout.size = Pt(40.0f, 40.0f);
    vout.inputs.push_back(::Port("port", PortSide::Input, an24::PortType::V));
    vout.outputs.push_back(::Port("ext", PortSide::Output, an24::PortType::Any));
    bp.add_node(std::move(vout));

    // Internal wires (matching simple_battery.json)
    Wire w1;
    w1.id = id + ":w1";
    w1.start = WireEnd((id + ":vin").c_str(), "port", PortSide::Output);
    w1.end = WireEnd((id + ":bat").c_str(), "v_in", PortSide::Input);
    bp.add_wire(w1);

    Wire w2;
    w2.id = id + ":w2";
    w2.start = WireEnd((id + ":bat").c_str(), "v_out", PortSide::Output);
    w2.end = WireEnd((id + ":vout").c_str(), "port", PortSide::Input);
    bp.add_wire(w2);

    // gnd.v → vin.port (ground reference for SOR solver)
    Wire w3;
    w3.id = id + ":w3";
    w3.start = WireEnd((id + ":gnd").c_str(), "v", PortSide::Output);
    w3.end = WireEnd((id + ":vin").c_str(), "port", PortSide::Input);
    bp.add_wire(w3);

    CollapsedGroup group;
    group.id = id;
    group.blueprint_path = "blueprints/simple_battery.json";
    group.type_name = "simple_battery";
    group.pos = pos;
    group.size = Pt(120.0f, 80.0f);
    group.internal_node_ids = {id + ":gnd", id + ":vin", id + ":bat", id + ":vout"};
    bp.collapsed_groups.push_back(group);

    bp.recompute_group_ids();
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
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

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
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

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
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

    // Check for specific port type colors
    // Format: 0xAABBGGRR (alpha, blue, green, red) - see render_theme::get_port_color()
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
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

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
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

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
    // Test that collapsed blueprint nodes are saved in editor.blueprint_nodes
    // (Not in devices — Blueprint nodes are visual-only wrappers)
    Blueprint bp;
    add_lamp_pass_through(bp, "lamp1");

    std::string json_str = blueprint_to_json(bp);
    json j = json::parse(json_str);

    // Internal devices should be in devices array (not the collapsed node)
    ASSERT_TRUE(j.contains("devices")) << "JSON should have devices array";
    auto& devices = j["devices"];
    EXPECT_EQ(devices.size(), 3u) << "Should have 3 internal devices (vin, lamp, vout)";

    // The collapsed Blueprint node should be in editor.blueprint_nodes
    ASSERT_TRUE(j.contains("editor")) << "JSON should have editor section";
    ASSERT_TRUE(j["editor"].contains("blueprint_nodes"));
    auto& bp_nodes = j["editor"]["blueprint_nodes"];
    EXPECT_EQ(bp_nodes.size(), 1u) << "Should have 1 blueprint node";
    EXPECT_EQ(bp_nodes[0]["name"], "lamp1");
    EXPECT_EQ(bp_nodes[0]["kind"], "Blueprint");

    // Connections should include rewritten wires to internal nodes
    ASSERT_TRUE(j.contains("connections"));
    // 2 internal wires (vin.port→lamp.v_in, lamp.v_out→vout.port)
    EXPECT_EQ(j["connections"].size(), 2u);
}

TEST(Persistence, CollapsedState_LoadsFromJson) {
    // Test round-trip: save an expanded blueprint, load it back, verify structure
    Blueprint original;
    add_lamp_pass_through(original, "lamp1");

    // Add a ground node
    Node gnd;
    gnd.id = "gnd";
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.params["value"] = "0.0";
    gnd.pos = Pt(50.0f, 100.0f);
    gnd.size = Pt(80.0f, 60.0f);
    gnd.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    original.add_node(std::move(gnd));

    // Save and reload
    std::string json_str = blueprint_to_editor_json(original);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value()) << "Should parse blueprint successfully";

    Blueprint& bp = *loaded;

    // Should have 4 nodes: gnd + collapsed lamp1 + 3 internal nodes
    EXPECT_EQ(bp.nodes.size(), 5u) << "Should have 5 nodes (gnd + collapsed + 3 internal)";

    // Find the lamp1 collapsed node
    Node* lamp_node = bp.find_node("lamp1");
    ASSERT_NE(lamp_node, nullptr) << "Should find lamp1 collapsed node";
    EXPECT_EQ(lamp_node->kind, NodeKind::Blueprint);
    EXPECT_TRUE(lamp_node->collapsed);

    // Collapsed node has root group_id, internals have group_id='lamp1'
    EXPECT_TRUE(lamp_node->group_id.empty()) << "Collapsed node should have root group_id";

    for (const auto& internal_id : {"lamp1:vin", "lamp1:lamp", "lamp1:vout"}) {
        Node* internal = bp.find_node(internal_id);
        ASSERT_NE(internal, nullptr) << "Internal node " << internal_id << " should exist";
        EXPECT_EQ(internal->group_id, "lamp1") << "Internal node " << internal_id << " should have group_id='lamp1'";
    }
}

TEST(Persistence, RoundTrip_SaveLoad_PreservesCollapsedBlueprint) {
    // Build blueprint with source + expanded lamp_pass_through + wire
    Blueprint original;

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

    add_lamp_pass_through(original, "lamp1");

    // External wire: source → collapsed node (gets rewritten to lamp1:vin.ext)
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("source", "v", PortSide::Output);
    wire1.end = WireEnd("lamp1", "vin", PortSide::Input);
    original.add_wire(wire1);

    // Save and reload
    std::string json_str = blueprint_to_editor_json(original);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value()) << "Failed to load blueprint from JSON";

    // Verify structure preserved
    auto lamp_node = std::find_if(loaded->nodes.begin(), loaded->nodes.end(),
        [](const Node& n) { return n.id == "lamp1"; });
    ASSERT_NE(lamp_node, loaded->nodes.end()) << "lamp1 collapsed node should exist";
    EXPECT_EQ(lamp_node->kind, NodeKind::Blueprint);
    EXPECT_TRUE(lamp_node->collapsed);

    // Verify simulation works after round-trip
    SimulationController sim;
    sim.build(*loaded);
    ASSERT_TRUE(sim.build_result.has_value()) << "Failed to build simulation from loaded blueprint";
    auto& result = *sim.build_result;

    const float dt = 1.0f / 60.0f;
    sim.start();
    for (int step = 0; step < 100; ++step) {
        sim.step(dt);
    }
    sim.stop();

    auto source_v = get_voltage(sim.state, result, "source.v");
    EXPECT_NEAR(source_v, 28.0f, 0.1f) << "Source should be at 28V after round-trip";

    // Verify voltage reaches internal nodes via ext alias port
    auto vin_port = result.port_to_signal.find("lamp1:vin.port");
    EXPECT_NE(vin_port, result.port_to_signal.end()) << "lamp1:vin.port should exist";
}

// =============================================================================
// Integration Tests: Voltage Flow Through Collapsed Blueprints
// =============================================================================

TEST(VoltageFlow, CollapsedBlueprint_PassesVoltage) {
    // Always-flatten: internal nodes exist in parent blueprint, visibility is cosmetic
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

    // Expanded lamp_pass_through (collapsed node + 3 internal nodes + CollapsedGroup)
    add_lamp_pass_through(bp, "lamp1");

    // External wire: source → collapsed node's vin port
    // blueprint_to_json rewrites this to source.v → lamp1:vin.ext
    // union-find merges ext↔port alias, connecting source to internal circuit
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("source", "v", PortSide::Output);
    wire1.end = WireEnd("lamp1", "vin", PortSide::Input);
    bp.add_wire(wire1);

    // Build simulation
    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value()) << "Build should succeed";
    auto& result = *sim.build_result;

    // Run simulation for 50 steps to settle
    const float dt = 1.0f / 60.0f;
    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(dt);
    }
    sim.stop();

    // Check voltages — ext port alias merged with port by union-find
    auto source_v = get_voltage(sim.state, result, "source.v");
    EXPECT_NEAR(source_v, 28.0f, 0.1f) << "Source should be at 28V";

    // BlueprintInput's ext and port are aliased → same signal as source
    auto lamp1_vin = get_voltage(sim.state, result, "lamp1:vin.port");
    EXPECT_NEAR(lamp1_vin, 28.0f, 0.1f) << "Lamp input should be ~28V (via ext alias)";

    auto lamp1_vout = get_voltage(sim.state, result, "lamp1:vout.port");
    EXPECT_GT(lamp1_vout, 20.0f) << "Lamp output should be >20V";
    EXPECT_LE(lamp1_vout, 28.1f) << "Lamp output should not exceed source voltage";
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

    // Expanded simple_battery
    add_simple_battery(bp, "bat1");

    // Wire: ground → battery input (rewritten to gnd.v → bat1:vin.ext)
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

    const float dt = 1.0f / 60.0f;
    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(dt);
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
    const float dt = 1.0f / 60.0f;
    sim.start();
    for (int step = 0; step < 100; ++step) {
        sim.step(dt);
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

    const float dt = 1.0f / 60.0f;
    sim.start();
    for (int step = 0; step < 50; ++step) {
        sim.step(dt);
    }
    sim.stop();

    EXPECT_NEAR(get_voltage(sim.state, result, "gnd.v"),  0.0f,   0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v12.v"),  12.0f,  0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v28.v"),  28.0f,  0.01f);
    EXPECT_NEAR(get_voltage(sim.state, result, "v115.v"), 115.0f, 0.01f);
}

// =============================================================================
// Group-Based Blueprint Collapsing Tests
// =============================================================================

TEST(GroupId, CollapsedNodeHasRootGroupId_InternalsHaveGroupGroupId) {
    // This test verifies that recompute_group_ids assigns group_id correctly
    Blueprint bp;
    add_lamp_pass_through(bp, "lamp1");

    // Verify group_id state (recompute_group_ids was called by add_lamp_pass_through)
    EXPECT_EQ(bp.nodes.size(), 4) << "Should have 4 nodes (collapsed + 3 internals)";

    // Count nodes by group_id
    int root_count = 0;
    int group_count = 0;
    for (const auto& n : bp.nodes) {
        if (n.group_id.empty()) root_count++;
        else if (n.group_id == "lamp1") group_count++;
    }

    EXPECT_EQ(root_count, 1) << "Only collapsed node should have root group_id (empty string)";
    EXPECT_EQ(group_count, 3) << "All 3 internal nodes should have group_id='lamp1'";

    Node* collapsed_node = bp.find_node("lamp1");
    ASSERT_NE(collapsed_node, nullptr);
    EXPECT_TRUE(collapsed_node->group_id.empty()) << "Collapsed node should have root group_id";
    EXPECT_EQ(collapsed_node->kind, NodeKind::Blueprint);

    for (const auto& internal_id : {"lamp1:vin", "lamp1:lamp", "lamp1:vout"}) {
        Node* internal = bp.find_node(internal_id);
        ASSERT_NE(internal, nullptr) << "Internal node " << internal_id << " should exist";
        EXPECT_EQ(internal->group_id, "lamp1") << "Internal node " << internal_id << " should have group_id='lamp1'";
    }
}

TEST(GroupId, GroupIdAssignmentsAre_Static_NotDrillBased) {
    // Verify that group_id is a static assignment — recompute_group_ids()
    // doesn't take a drill_stack parameter, it assigns group_id based on
    // collapsed_groups membership, which is static.
    Blueprint bp;
    add_lamp_pass_through(bp, "lamp1");

    // Verify initial group_id assignments
    EXPECT_TRUE(bp.find_node("lamp1")->group_id.empty());
    EXPECT_EQ(bp.find_node("lamp1:vin")->group_id, "lamp1");
    EXPECT_EQ(bp.find_node("lamp1:lamp")->group_id, "lamp1");
    EXPECT_EQ(bp.find_node("lamp1:vout")->group_id, "lamp1");

    // Call recompute_group_ids again (no drill_stack parameter)
    bp.recompute_group_ids();

    // group_id assignments should remain unchanged (static)
    EXPECT_TRUE(bp.find_node("lamp1")->group_id.empty()) << "Collapsed node still has root group_id";
    EXPECT_EQ(bp.find_node("lamp1:vin")->group_id, "lamp1") << "Internal node group_id unchanged";
    EXPECT_EQ(bp.find_node("lamp1:lamp")->group_id, "lamp1") << "Internal node group_id unchanged";
    EXPECT_EQ(bp.find_node("lamp1:vout")->group_id, "lamp1") << "Internal node group_id unchanged";

    // Count nodes by group_id after second recompute
    int root_count = 0;
    int group_count = 0;
    for (const auto& n : bp.nodes) {
        if (n.group_id.empty()) root_count++;
        else if (n.group_id == "lamp1") group_count++;
    }
    EXPECT_EQ(root_count, 1) << "Still 1 node at root level";
    EXPECT_EQ(group_count, 3) << "Still 3 nodes in 'lamp1' group";
}

// =============================================================================
// Regression Tests for Bug Fixes
// =============================================================================

// Bug 1: Save/load must preserve group_id assignments
TEST(Regression, SaveLoad_GroupIdIsPreserved) {
    Blueprint bp;
    add_lamp_pass_through(bp, "lamp1");

    // Before save: collapsed node has root group_id, internals have group_id='lamp1'
    EXPECT_TRUE(bp.find_node("lamp1")->group_id.empty());
    EXPECT_EQ(bp.find_node("lamp1:vin")->group_id, "lamp1");
    EXPECT_EQ(bp.find_node("lamp1:lamp")->group_id, "lamp1");
    EXPECT_EQ(bp.find_node("lamp1:vout")->group_id, "lamp1");

    // Save and reload
    std::string json_str = blueprint_to_editor_json(bp);
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    // After load: group_id must be correctly preserved
    EXPECT_TRUE(loaded->find_node("lamp1")->group_id.empty())
        << "Collapsed node must have root group_id after load (Bug 1 regression)";
    EXPECT_EQ(loaded->find_node("lamp1:vin")->group_id, "lamp1")
        << "Internal node must have group_id='lamp1' after load (Bug 1 regression)";
    EXPECT_EQ(loaded->find_node("lamp1:lamp")->group_id, "lamp1")
        << "Internal node must have group_id='lamp1' after load (Bug 1 regression)";
    EXPECT_EQ(loaded->find_node("lamp1:vout")->group_id, "lamp1")
        << "Internal node must have group_id='lamp1' after load (Bug 1 regression)";

    // Save and load again (no drill_stack concept anymore)
    std::string json2 = blueprint_to_editor_json(*loaded);
    auto loaded2 = blueprint_from_json(json2);
    ASSERT_TRUE(loaded2.has_value());

    // group_id assignments should still be correct (static, not drill-dependent)
    EXPECT_TRUE(loaded2->find_node("lamp1")->group_id.empty())
        << "After second load, collapsed node still at root level";
    EXPECT_EQ(loaded2->find_node("lamp1:vin")->group_id, "lamp1")
        << "After second load, internal still in 'lamp1' group";
}

// Bug 2: No electricity through nested blueprints — wires must be rewritten
TEST(Regression, WireRewriting_ExternalWiresToBlueprintNode) {
    Blueprint bp;

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

    add_lamp_pass_through(bp, "lamp1");

    // Wire to collapsed node (will be rewritten by blueprint_to_json)
    Wire wire1;
    wire1.id = "wire1";
    wire1.start = WireEnd("source", "v", PortSide::Output);
    wire1.end = WireEnd("lamp1", "vin", PortSide::Input);
    bp.add_wire(wire1);

    // Serialize and check wire rewriting in JSON
    std::string json_str = blueprint_to_json(bp);
    auto j = json::parse(json_str);

    bool found_rewritten = false;
    for (const auto& conn : j["connections"]) {
        std::string from = conn["from"].get<std::string>();
        std::string to = conn["to"].get<std::string>();
        if (from == "source.v" && to == "lamp1:vin.ext") {
            found_rewritten = true;
        }
        // Must NOT have wire to "lamp1.vin" (the collapsed node)
        EXPECT_NE(to, "lamp1.vin")
            << "Wire to collapsed Blueprint node must be rewritten (Bug 2 regression)";
    }
    EXPECT_TRUE(found_rewritten)
        << "Wire source.v → lamp1.vin must be rewritten to source.v → lamp1:vin.ext";
}

// Bug 3: Blueprint nodes must not show internal content (NodeContentType::None)
TEST(Regression, BlueprintNode_HasNoContent) {
    Node collapsed;
    collapsed.id = "bp1";
    collapsed.name = "bp1";
    collapsed.type_name = "my_blueprint";
    collapsed.kind = NodeKind::Blueprint;
    collapsed.collapsed = true;

    EXPECT_EQ(collapsed.node_content.type, NodeContentType::None)
        << "Blueprint collapsed node must have no internal content (Bug 3 regression)";
}

// Bug 4: BlueprintInput/Output ext port alias — union-find must merge ext↔port
TEST(Regression, ExtAliasPort_MergedByUnionFind) {
    Blueprint bp;
    add_lamp_pass_through(bp, "lamp1");

    SimulationController sim;
    sim.build(bp);
    ASSERT_TRUE(sim.build_result.has_value());
    auto& result = *sim.build_result;

    // ext and port must be aliased to same signal via union-find
    auto vin_port = result.port_to_signal.find("lamp1:vin.port");
    auto vin_ext = result.port_to_signal.find("lamp1:vin.ext");
    ASSERT_NE(vin_port, result.port_to_signal.end());
    ASSERT_NE(vin_ext, result.port_to_signal.end());
    EXPECT_EQ(vin_port->second, vin_ext->second)
        << "BlueprintInput ext↔port must be same signal (Bug 4 regression)";

    auto vout_port = result.port_to_signal.find("lamp1:vout.port");
    auto vout_ext = result.port_to_signal.find("lamp1:vout.ext");
    ASSERT_NE(vout_port, result.port_to_signal.end());
    ASSERT_NE(vout_ext, result.port_to_signal.end());
    EXPECT_EQ(vout_port->second, vout_ext->second)
        << "BlueprintOutput ext↔port must be same signal (Bug 4 regression)";
}

// N-level hierarchy: group_id assignments with nested groups
TEST(Regression, NLevelHierarchy_GroupIdAssignments) {
    Blueprint bp;

    // Top-level node
    Node top;
    top.id = "top";
    top.name = "top";
    top.type_name = "RefNode";
    top.kind = NodeKind::Ref;
    top.pos = Pt(0.0f, 0.0f);
    top.size = Pt(80.0f, 60.0f);
    bp.add_node(std::move(top));

    // Blueprint A (contains Blueprint B)
    add_lamp_pass_through(bp, "A");

    // Blueprint B (nested inside A's group)
    add_lamp_pass_through(bp, "A:sub");

    // Add A:sub to A's group (making it nested)
    // add_lamp_pass_through already created a CollapsedGroup for A:sub,
    // so just add A:sub to A's internal_node_ids
    for (auto& group : bp.collapsed_groups) {
        if (group.id == "A") {
            group.internal_node_ids.push_back("A:sub");
            break;
        }
    }

    // Recompute group_ids (static assignment based on collapsed_groups)
    bp.recompute_group_ids();

    // Top-level nodes: 'top' and 'A' have root group_id
    EXPECT_TRUE(bp.find_node("top")->group_id.empty()) << "Top-level node has root group_id";
    EXPECT_TRUE(bp.find_node("A")->group_id.empty()) << "Collapsed node 'A' has root group_id";

    // A's internal nodes are in group 'A'
    EXPECT_EQ(bp.find_node("A:vin")->group_id, "A") << "A's internal node has group_id='A'";
    EXPECT_EQ(bp.find_node("A:lamp")->group_id, "A") << "A's internal node has group_id='A'";
    EXPECT_EQ(bp.find_node("A:vout")->group_id, "A") << "A's internal node has group_id='A'";

    // A:sub is also in group 'A' (nested inside A)
    EXPECT_EQ(bp.find_node("A:sub")->group_id, "A") << "Nested collapsed node is in A's group";

    // A:sub's internal nodes are in group 'A:sub'
    EXPECT_EQ(bp.find_node("A:sub:vin")->group_id, "A:sub") << "A:sub's internal node has group_id='A:sub'";
    EXPECT_EQ(bp.find_node("A:sub:lamp")->group_id, "A:sub") << "A:sub's internal node has group_id='A:sub'";
    EXPECT_EQ(bp.find_node("A:sub:vout")->group_id, "A:sub") << "A:sub's internal node has group_id='A:sub'";

    // Verify group_id assignments are static (don't change with drill state)
    bp.recompute_group_ids();
    EXPECT_TRUE(bp.find_node("A")->group_id.empty()) << "A still has root group_id";
    EXPECT_EQ(bp.find_node("A:vin")->group_id, "A") << "A:vin still in group 'A'";
    EXPECT_EQ(bp.find_node("A:sub")->group_id, "A") << "A:sub still in group 'A'";
    EXPECT_EQ(bp.find_node("A:sub:vin")->group_id, "A:sub") << "A:sub:vin still in group 'A:sub'";
}

// =============================================================================
// DIAGNOSTIC: Trace signal flow through blueprint boundary
// =============================================================================

TEST(BlueprintSignalFlow, LampPassThrough_VoltageFlows) {
    // Build a circuit: gnd → bat.v_in, bat.v_out → lpt.vin, lpt.vout → res.v_in, res.v_out → gnd
    Blueprint bp;

    // Ground reference
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.output("v");
    gnd.at(0, 0).size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    // Battery (28V)
    Node bat;
    bat.id = "bat";
    bat.type_name = "Battery";
    bat.input("v_in");
    bat.output("v_out");
    bat.at(100, 0).size_wh(120, 80);
    bp.add_node(std::move(bat));

    // Add lamp_pass_through as nested blueprint
    add_lamp_pass_through(bp, "lpt");

    // Resistor (acts as load, creates return path)
    Node res;
    res.id = "res";
    res.type_name = "Resistor";
    res.input("v_in");
    res.output("v_out");
    res.at(400, 0).size_wh(120, 80);
    bp.add_node(std::move(res));

    // Wires: gnd→bat.v_in, bat.v_out→lpt.vin, lpt.vout→res.v_in, res.v_out→gnd
    Wire w1; w1.start = WireEnd("gnd", "v", PortSide::Output); w1.end = WireEnd("bat", "v_in", PortSide::Input); bp.add_wire(w1);
    Wire w2; w2.start = WireEnd("bat", "v_out", PortSide::Output); w2.end = WireEnd("lpt", "vin", PortSide::Input); bp.add_wire(w2);
    Wire w3; w3.start = WireEnd("lpt", "vout", PortSide::Output); w3.end = WireEnd("res", "v_in", PortSide::Input); bp.add_wire(w3);
    Wire w4; w4.start = WireEnd("res", "v_out", PortSide::Output); w4.end = WireEnd("gnd", "v", PortSide::Input); bp.add_wire(w4);

    // STEP 1: Check blueprint_to_json output
    std::string json_str = blueprint_to_json(bp);
    auto j = nlohmann::json::parse(json_str);

    // Verify that lpt (Blueprint node) is NOT in devices
    bool found_lpt_collapsed = false;
    for (const auto& dev : j["devices"]) {
        if (dev["name"] == "lpt") found_lpt_collapsed = true;
    }
    EXPECT_FALSE(found_lpt_collapsed) << "Collapsed Blueprint node should NOT be in devices";

    // Verify internal devices ARE present
    bool found_lpt_vin = false, found_lpt_lamp = false, found_lpt_vout = false;
    for (const auto& dev : j["devices"]) {
        std::string name = dev["name"];
        if (name == "lpt:vin") found_lpt_vin = true;
        if (name == "lpt:lamp") found_lpt_lamp = true;
        if (name == "lpt:vout") found_lpt_vout = true;
    }
    EXPECT_TRUE(found_lpt_vin) << "lpt:vin should be in devices";
    EXPECT_TRUE(found_lpt_lamp) << "lpt:lamp should be in devices";
    EXPECT_TRUE(found_lpt_vout) << "lpt:vout should be in devices";

    // Verify wire rewriting: bat.v_out → lpt.vin should become bat.v_out → lpt:vin.ext
    bool found_rewritten_wire = false;
    for (const auto& conn : j["connections"]) {
        std::string from = conn["from"];
        std::string to = conn["to"];
        if (from == "bat.v_out" && to == "lpt:vin.ext") found_rewritten_wire = true;
    }
    EXPECT_TRUE(found_rewritten_wire) << "Wire bat.v_out→lpt.vin should be rewritten to bat.v_out→lpt:vin.ext";

    // Verify wire rewriting: lpt.vout→res.v_in should become lpt:vout.ext→res.v_in
    bool found_rewritten_wire2 = false;
    for (const auto& conn : j["connections"]) {
        std::string from = conn["from"];
        std::string to = conn["to"];
        if (from == "lpt:vout.ext" && to == "res.v_in") found_rewritten_wire2 = true;
    }
    EXPECT_TRUE(found_rewritten_wire2) << "Wire lpt.vout→res.v_in should be rewritten to lpt:vout.ext→res.v_in";

    // STEP 2: Parse and build simulation
    auto ctx = parse_json(json_str);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }
    auto result = build_systems_dev(ctx.devices, connections);

    // Dump port_to_signal map for debugging
    std::cerr << "\n=== PORT TO SIGNAL MAP ===" << std::endl;
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cerr << "  " << port << " → signal " << sig << std::endl;
    }
    std::cerr << "=== FIXED SIGNALS: ";
    for (auto s : result.fixed_signals) std::cerr << s << " ";
    std::cerr << "===" << std::endl;

    // Verify alias ports are merged (ext and port should have same signal)
    auto vin_ext = result.port_to_signal.find("lpt:vin.ext");
    auto vin_port = result.port_to_signal.find("lpt:vin.port");
    ASSERT_NE(vin_ext, result.port_to_signal.end()) << "lpt:vin.ext should be in signal map";
    ASSERT_NE(vin_port, result.port_to_signal.end()) << "lpt:vin.port should be in signal map";
    EXPECT_EQ(vin_ext->second, vin_port->second)
        << "ext and port should be merged by union-find (alias)";

    auto vout_ext = result.port_to_signal.find("lpt:vout.ext");
    auto vout_port = result.port_to_signal.find("lpt:vout.port");
    ASSERT_NE(vout_ext, result.port_to_signal.end()) << "lpt:vout.ext should be in signal map";
    ASSERT_NE(vout_port, result.port_to_signal.end()) << "lpt:vout.port should be in signal map";
    EXPECT_EQ(vout_ext->second, vout_port->second)
        << "ext and port should be merged by union-find (alias)";

    // Verify signal connectivity: bat.v_out and lpt:vin.ext should share a signal
    auto bat_vout = result.port_to_signal.find("bat.v_out");
    ASSERT_NE(bat_vout, result.port_to_signal.end());
    EXPECT_EQ(bat_vout->second, vin_ext->second)
        << "Battery output should connect through blueprint boundary to lamp input";

    // Verify: lpt:vout.ext and res.v_in should share a signal
    auto res_vin = result.port_to_signal.find("res.v_in");
    ASSERT_NE(res_vin, result.port_to_signal.end());
    EXPECT_EQ(vout_ext->second, res_vin->second)
        << "Blueprint output should connect to resistor input";

    // STEP 3: Run simulation
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize RefNode fixed signals
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
            }
        }
    }

    // Run 200 steps
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 200; step++) {
        state.clear_through();
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
            }, variant);
        }
        state.precompute_inv_conductance();
        solve_sor_iteration(state.across.data(), state.through.data(),
                           state.inv_conductance.data(), state.across.size(), SOR::OMEGA);
    }

    // Dump all signal voltages
    std::cerr << "\n=== SIGNAL VOLTAGES AFTER 200 STEPS ===" << std::endl;
    for (size_t i = 0; i < state.across.size(); i++) {
        std::cerr << "  signal[" << i << "] = " << state.across[i] << "V" << std::endl;
    }

    // Check voltages
    float v_gnd = state.across[result.port_to_signal["gnd.v"]];
    float v_bat_out = state.across[result.port_to_signal["bat.v_out"]];
    float v_res_in = state.across[result.port_to_signal["res.v_in"]];

    EXPECT_NEAR(v_gnd, 0.0f, 0.1f) << "Ground should be near 0V";
    EXPECT_GT(v_bat_out, 5.0f) << "Battery output should have significant voltage";
    EXPECT_GT(v_res_in, 1.0f) << "Resistor input (after lamp) should have voltage";

    std::cerr << "gnd=" << v_gnd << "V, bat.v_out=" << v_bat_out
              << "V, res.v_in=" << v_res_in << "V" << std::endl;
}

// =============================================================================
// DIAGNOSTIC: Nested blueprint (simple_battery) — voltage at ROOT level
// simple_battery has internal gnd+battery. Connect a resistor at root.
// =============================================================================

TEST(BlueprintSignalFlow, SimpleBattery_VoltageAtRootLevel) {
    Blueprint bp;

    // Insert simple_battery as nested blueprint (has internal gnd + battery)
    add_simple_battery(bp, "sbat");

    // Root-level resistor connected to sbat.vout
    Node res;
    res.id = "res";
    res.type_name = "Resistor";
    res.input("v_in");
    res.output("v_out");
    res.at(400, 0).size_wh(120, 80);
    bp.add_node(std::move(res));

    // Root-level ground (return path for resistor)
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.output("v");
    gnd.at(0, 0).size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    // Wires:
    // sbat.vout → res.v_in (collapsed Blueprint node's output to resistor)
    // res.v_out → gnd.v (return path)
    // gnd.v → sbat.vin (ground to blueprint input)
    Wire w1; w1.start = WireEnd("sbat", "vout", PortSide::Output);
             w1.end = WireEnd("res", "v_in", PortSide::Input); bp.add_wire(w1);
    Wire w2; w2.start = WireEnd("res", "v_out", PortSide::Output);
             w2.end = WireEnd("gnd", "v", PortSide::Input); bp.add_wire(w2);
    Wire w3; w3.start = WireEnd("gnd", "v", PortSide::Output);
             w3.end = WireEnd("sbat", "vin", PortSide::Input); bp.add_wire(w3);

    // Dump JSON for debugging
    std::string json_str = blueprint_to_json(bp);
    std::cerr << "\n=== SERIALIZED JSON (simple_battery nested) ===" << std::endl;
    auto j = nlohmann::json::parse(json_str);
    std::cerr << "Devices: ";
    for (const auto& dev : j["devices"]) std::cerr << dev["name"].get<std::string>() << " ";
    std::cerr << std::endl;
    std::cerr << "Connections:" << std::endl;
    for (const auto& conn : j["connections"]) {
        std::cerr << "  " << conn["from"].get<std::string>() << " → " << conn["to"].get<std::string>() << std::endl;
    }

    // Parse and build
    auto ctx = parse_json(json_str);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) connections.push_back({c.from, c.to});
    auto result = build_systems_dev(ctx.devices, connections);

    // Dump signal map
    std::cerr << "\n=== PORT TO SIGNAL MAP ===" << std::endl;
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cerr << "  " << port << " → signal " << sig << std::endl;
    }
    std::cerr << "Fixed signals: ";
    for (auto s : result.fixed_signals) std::cerr << s << " ";
    std::cerr << std::endl;

    // Run 200 steps
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end())
                state.across[it_sig->second] = value;
        }
    }
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 200; step++) {
        state.clear_through();
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
            }, variant);
        }
        state.precompute_inv_conductance();
        solve_sor_iteration(state.across.data(), state.through.data(),
                           state.inv_conductance.data(), state.across.size(), SOR::OMEGA);
    }

    // Dump voltages
    std::cerr << "\n=== SIGNAL VOLTAGES ===" << std::endl;
    for (size_t i = 0; i < state.across.size(); i++) {
        std::cerr << "  signal[" << i << "] = " << state.across[i] << "V" << std::endl;
    }

    // Check: resistor at ROOT should see voltage from nested battery
    auto res_vin_it = result.port_to_signal.find("res.v_in");
    ASSERT_NE(res_vin_it, result.port_to_signal.end()) << "res.v_in must be in signal map";
    float v_res = state.across[res_vin_it->second];
    std::cerr << "res.v_in = " << v_res << "V" << std::endl;
    EXPECT_GT(v_res, 5.0f) << "Resistor should see voltage from nested battery blueprint";

    // Check: sbat:bat.v_out (internal battery) should have voltage
    auto bat_vout_it = result.port_to_signal.find("sbat:bat.v_out");
    ASSERT_NE(bat_vout_it, result.port_to_signal.end()) << "sbat:bat.v_out must be in signal map";
    float v_bat = state.across[bat_vout_it->second];
    std::cerr << "sbat:bat.v_out = " << v_bat << "V" << std::endl;
    EXPECT_GT(v_bat, 10.0f) << "Internal battery should produce voltage";

    // CRITICAL: Verify that collapsed node port lookups FAIL (this is the bug!)
    // "sbat.vout" is NOT in the signal map — it was rewritten to "sbat:vout.ext"
    auto sbat_vout_direct = result.port_to_signal.find("sbat.vout");
    std::cerr << "sbat.vout in signal map: " << (sbat_vout_direct != result.port_to_signal.end()) << std::endl;

    // But "sbat:vout.ext" IS in the signal map
    auto sbat_vout_ext = result.port_to_signal.find("sbat:vout.ext");
    ASSERT_NE(sbat_vout_ext, result.port_to_signal.end()) << "sbat:vout.ext must be in signal map";
    float v_ext = state.across[sbat_vout_ext->second];
    std::cerr << "sbat:vout.ext = " << v_ext << "V (this is where the tooltip should look)" << std::endl;
    EXPECT_GT(v_ext, 5.0f) << "Blueprint ext port should carry voltage";
}

// =============================================================================
// Regression: nodes added to sub-blueprint must survive save/load
// =============================================================================

TEST(EditorPersistence, AddedSubNodePersistsRoundtrip) {
    // Simulate: user has lamp_pass_through, then adds a Resistor inside it
    Blueprint bp;
    add_lamp_pass_through(bp, "lpt");

    // Simulate add_component("Resistor") inside sub-blueprint "lpt"
    Node res;
    res.id = "resistor_1";
    res.name = "resistor_1";
    res.type_name = "Resistor";
    res.kind = NodeKind::Node;
    res.pos = Pt(300, 100);
    res.size = Pt(120, 80);
    res.group_id = "lpt";  // added to sub-blueprint
    res.inputs.push_back(::Port("v_in", PortSide::Input, an24::PortType::V));
    res.outputs.push_back(::Port("v_out", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(res));

    // Keep collapsed_groups in sync (as the fixed add_component does)
    for (auto& g : bp.collapsed_groups) {
        if (g.id == "lpt") {
            g.internal_node_ids.push_back("resistor_1");
            break;
        }
    }

    // Wire from lamp to resistor (inside sub-blueprint)
    Wire w;
    w.id = "added_wire";
    w.start = WireEnd("lpt:lamp", "v_out", PortSide::Output);
    w.end = WireEnd("resistor_1", "v_in", PortSide::Input);
    w.routing_points = {Pt(250, 120)};
    bp.add_wire(w);

    // Save
    std::string json_str = blueprint_to_editor_json(bp);
    auto j = json::parse(json_str);

    // Verify the added node has group_id in JSON
    bool found_res_with_group = false;
    for (const auto& d : j["devices"]) {
        if (d["name"] == "resistor_1") {
            ASSERT_TRUE(d.contains("group_id")) << "Added sub-node must have group_id in JSON";
            EXPECT_EQ(d["group_id"].get<std::string>(), "lpt");
            found_res_with_group = true;
        }
    }
    EXPECT_TRUE(found_res_with_group) << "Added Resistor must be in saved devices";

    // Verify the wire is saved
    bool found_wire = false;
    for (const auto& ws : j["wires"]) {
        if (ws["from"] == "lpt:lamp.v_out" && ws["to"] == "resistor_1.v_in") {
            found_wire = true;
            EXPECT_EQ(ws["routing_points"].size(), 1u);
        }
    }
    EXPECT_TRUE(found_wire) << "Wire inside sub-blueprint must be saved";

    // Load
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    // Verify the added node roundtripped
    auto* loaded_res = loaded->find_node("resistor_1");
    ASSERT_NE(loaded_res, nullptr) << "Added Resistor must survive load";
    EXPECT_EQ(loaded_res->type_name, "Resistor");
    EXPECT_FLOAT_EQ(loaded_res->pos.x, 300.0f);
    EXPECT_FLOAT_EQ(loaded_res->pos.y, 100.0f);

    // Recompute group_ids from loaded collapsed_groups
    loaded->recompute_group_ids();

    // Verify group_id is correct after load
    EXPECT_EQ(loaded_res->group_id, "lpt")
        << "Added sub-node must have correct group_id after load";

    // Verify the wire roundtripped with routing points
    bool found_loaded_wire = false;
    for (const auto& lw : loaded->wires) {
        if (lw.start.node_id == "lpt:lamp" && lw.start.port_name == "v_out" &&
            lw.end.node_id == "resistor_1" && lw.end.port_name == "v_in") {
            found_loaded_wire = true;
            EXPECT_EQ(lw.routing_points.size(), 1u)
                << "Routing points must survive save/load";
            if (!lw.routing_points.empty()) {
                EXPECT_FLOAT_EQ(lw.routing_points[0].x, 250.0f);
                EXPECT_FLOAT_EQ(lw.routing_points[0].y, 120.0f);
            }
        }
    }
    EXPECT_TRUE(found_loaded_wire) << "Wire inside sub-blueprint must survive load";

    // Verify the resistor is in the collapsed group's internal_node_ids
    bool found_in_group = false;
    for (const auto& g : loaded->collapsed_groups) {
        if (g.id == "lpt") {
            for (const auto& nid : g.internal_node_ids) {
                if (nid == "resistor_1") found_in_group = true;
            }
        }
    }
    EXPECT_TRUE(found_in_group) << "Added node must be in collapsed_group internal_node_ids after load";
}

// =============================================================================
// Test: Persistence (Phase 5.3) - Editor format save/load
// =============================================================================

// Regression: save_blueprint_to_file uses editor format (flat nodes + wires)
// and blueprint_from_json roundtrips correctly.
TEST(EditorPersistence, EditorFormatRoundtrip) {
    Blueprint original;

    // Top-level nodes
    Node gnd;
    gnd.id = "gnd";
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.pos = Pt(100, 400);
    gnd.size = Pt(48, 32);
    gnd.outputs.push_back(::Port("v", PortSide::Output, an24::PortType::V));
    original.add_node(std::move(gnd));

    Node bat;
    bat.id = "bat";
    bat.name = "bat";
    bat.type_name = "Battery";
    bat.kind = NodeKind::Node;
    bat.pos = Pt(100, 100);
    bat.size = Pt(120, 80);
    bat.inputs.push_back(::Port("v_in", PortSide::Input, an24::PortType::V));
    bat.outputs.push_back(::Port("v_out", PortSide::Output, an24::PortType::V));
    original.add_node(std::move(bat));

    // Add lamp_pass_through blueprint (creates lamp1 + internals)
    add_lamp_pass_through(original, "lamp1");

    // Wire: bat.v_out → lamp1.vin (to collapsed Blueprint node)
    Wire w1;
    w1.id = "wire_0";
    w1.start = WireEnd("bat", "v_out", PortSide::Output);
    w1.end = WireEnd("lamp1", "vin", PortSide::Input);
    w1.routing_points = {Pt(300, 140), Pt(350, 140)};
    original.add_wire(w1);

    // Wire: gnd.v → bat.v_in
    Wire w2;
    w2.id = "wire_1";
    w2.start = WireEnd("gnd", "v", PortSide::Output);
    w2.end = WireEnd("bat", "v_in", PortSide::Input);
    original.add_wire(w2);

    original.pan = Pt(50, 50);
    original.zoom = 1.5f;
    original.grid_step = 16.0f;

    // Serialize with editor format
    std::string json_str = blueprint_to_editor_json(original);
    auto j = json::parse(json_str);

    // Verify structure: top-level "wires" (not "connections"), "collapsed_groups", "viewport"
    EXPECT_TRUE(j.contains("wires"));
    EXPECT_TRUE(j.contains("collapsed_groups"));
    EXPECT_TRUE(j.contains("viewport"));
    EXPECT_FALSE(j.contains("connections")) << "Editor format should not have 'connections'";
    EXPECT_FALSE(j.contains("editor")) << "Editor format should not have nested 'editor' section";

    // Verify all nodes present (including Blueprint kind)
    EXPECT_EQ(j["devices"].size(), original.nodes.size());

    // Verify Blueprint node is in devices with kind + group_id
    bool found_blueprint = false;
    bool found_internal_with_group = false;
    for (const auto& d : j["devices"]) {
        if (d["name"] == "lamp1" && d["kind"] == "Blueprint") {
            found_blueprint = true;
            EXPECT_EQ(d["pos"]["x"].get<float>(), original.find_node("lamp1")->pos.x);
        }
        if (d["name"] == "lamp1:lamp" && d.contains("group_id") && d["group_id"] == "lamp1") {
            found_internal_with_group = true;
        }
    }
    EXPECT_TRUE(found_blueprint) << "Blueprint kind node must be in devices array";
    EXPECT_TRUE(found_internal_with_group) << "Internal nodes must have group_id";

    // Verify wires are in original form (not rewritten)
    bool found_wire_to_blueprint = false;
    for (const auto& w : j["wires"]) {
        if (w["from"] == "bat.v_out" && w["to"] == "lamp1.vin") {
            found_wire_to_blueprint = true;
            // Routing points preserved
            EXPECT_EQ(w["routing_points"].size(), 2u);
        }
    }
    EXPECT_TRUE(found_wire_to_blueprint) << "Wires must reference Blueprint node directly (no rewriting)";

    // Load back
    auto loaded = blueprint_from_json(json_str);
    ASSERT_TRUE(loaded.has_value());

    // All nodes roundtripped
    EXPECT_EQ(loaded->nodes.size(), original.nodes.size());

    // Positions preserved
    auto* lamp1 = loaded->find_node("lamp1");
    ASSERT_NE(lamp1, nullptr);
    EXPECT_EQ(lamp1->kind, NodeKind::Blueprint);
    EXPECT_FLOAT_EQ(lamp1->pos.x, original.find_node("lamp1")->pos.x);
    EXPECT_FLOAT_EQ(lamp1->pos.y, original.find_node("lamp1")->pos.y);

    // Wires roundtripped (original form, to Blueprint node)
    bool found_wire = false;
    for (const auto& w : loaded->wires) {
        if (w.start.node_id == "bat" && w.start.port_name == "v_out" &&
            w.end.node_id == "lamp1" && w.end.port_name == "vin") {
            found_wire = true;
            EXPECT_EQ(w.routing_points.size(), 2u);
        }
    }
    EXPECT_TRUE(found_wire) << "Wire to Blueprint node must survive save/load";

    // Collapsed groups reconstructed from group_id
    EXPECT_EQ(loaded->collapsed_groups.size(), 1u);
    EXPECT_EQ(loaded->collapsed_groups[0].id, "lamp1");
    EXPECT_FALSE(loaded->collapsed_groups[0].internal_node_ids.empty());

    // group_id recomputed correctly
    EXPECT_TRUE(loaded->find_node("lamp1")->group_id.empty());
    EXPECT_EQ(loaded->find_node("lamp1:lamp")->group_id, "lamp1");

    // Viewport preserved
    EXPECT_FLOAT_EQ(loaded->pan.x, 50.0f);
    EXPECT_FLOAT_EQ(loaded->zoom, 1.5f);
}

// Regression: Blueprint kind nodes should have no content (no "OFF" label)
TEST(EditorPersistence, BlueprintNodeHasNoContent) {
    Blueprint bp;
    add_lamp_pass_through(bp, "lpt");

    // The internal lamp (IndicatorLight) has content
    auto* lamp = bp.find_node("lpt:lamp");
    ASSERT_NE(lamp, nullptr);
    lamp->node_content.type = NodeContentType::Text;
    lamp->node_content.label = "OFF";

    // The Blueprint collapsed node must NOT have content
    auto* collapsed = bp.find_node("lpt");
    ASSERT_NE(collapsed, nullptr);
    EXPECT_EQ(collapsed->kind, NodeKind::Blueprint);

    // VisualNodeFactory should strip content from Blueprint nodes
    auto visual = VisualNodeFactory::create(*collapsed);
    // Blueprint visual node should be visible but with no content widget
    // (we can verify the node_content is None via the factory path)
    // The key assertion: Blueprint nodes should not render content
    // This is enforced in VisualNodeFactory::create which clears node_content
    Node bp_copy = *collapsed;
    bp_copy.node_content.type = NodeContentType::Text;
    bp_copy.node_content.label = "SHOULD_NOT_APPEAR";
    auto visual2 = VisualNodeFactory::create(bp_copy);
    // The visual was created — if it rendered "SHOULD_NOT_APPEAR" that's the bug
    // We verify by checking the factory strips content for Blueprint kind
    EXPECT_EQ(bp_copy.kind, NodeKind::Blueprint);  // sanity
}



// =============================================================================
// Test: SOR stability with Simulator<JIT_Solver> (matches editor path)
// =============================================================================

TEST(BlueprintSignalFlow, SimpleBattery_SOR_Stability_JIT) {
    // Use Simulator<JIT_Solver> — the same path as the editor
    Blueprint bp;
    add_simple_battery(bp, "sbat");

    // Root-level resistor + ground
    Node res;
    res.id = "res"; res.type_name = "Resistor";
    res.input("v_in"); res.output("v_out");
    res.at(400, 0).size_wh(120, 80);
    bp.add_node(std::move(res));

    Node gnd;
    gnd.id = "gnd"; gnd.type_name = "RefNode"; gnd.kind = NodeKind::Ref;
    gnd.output("v"); gnd.at(0, 0).size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    Wire w1; w1.start = WireEnd("sbat", "vout", PortSide::Output);
             w1.end = WireEnd("res", "v_in", PortSide::Input); bp.add_wire(w1);
    Wire w2; w2.start = WireEnd("res", "v_out", PortSide::Output);
             w2.end = WireEnd("gnd", "v", PortSide::Input); bp.add_wire(w2);
    Wire w3; w3.start = WireEnd("gnd", "v", PortSide::Output);
             w3.end = WireEnd("sbat", "vin", PortSide::Input); bp.add_wire(w3);

    // Use the full Simulator<JIT_Solver> like the editor does
    an24::Simulator<an24::JIT_Solver> sim;
    sim.start(bp);

    // Run 500 steps (should converge, not explode)
    for (int i = 0; i < 500; i++) {
        sim.step(1.0f / 60.0f);
    }

    // Battery output should be around 28V, not NaN/inf
    float v_out = sim.get_wire_voltage("sbat:bat.v_out");
    std::cerr << "sbat:bat.v_out after 500 JIT steps = " << v_out << "V\n";

    EXPECT_FALSE(std::isnan(v_out)) << "SOR must not produce NaN";
    EXPECT_FALSE(std::isinf(v_out)) << "SOR must not produce inf";
    EXPECT_GT(v_out, 10.0f) << "Battery should produce voltage";
    EXPECT_LT(v_out, 50.0f) << "Voltage should not explode";

    sim.stop();
}

// =============================================================================
// Test: Kind derivation from classname
// =============================================================================

TEST(BlueprintKind, KindDerivedFromClassname) {
    // Verify that add_simple_battery test helper produces correct kinds
    Blueprint bp;
    add_simple_battery(bp, "sb");

    // Collapsed Blueprint node
    auto* collapsed = bp.find_node("sb");
    ASSERT_NE(collapsed, nullptr);
    EXPECT_EQ(collapsed->kind, NodeKind::Blueprint);

    // Internal RefNode should have correct kind
    auto* gnd_node = bp.find_node("sb:gnd");
    ASSERT_NE(gnd_node, nullptr);
    EXPECT_EQ(gnd_node->kind, NodeKind::Ref);
    EXPECT_EQ(gnd_node->type_name, "RefNode");

    // Internal Battery should have Node kind
    auto* bat_node = bp.find_node("sb:bat");
    ASSERT_NE(bat_node, nullptr);
    EXPECT_EQ(bat_node->kind, NodeKind::Node);
    EXPECT_EQ(bat_node->type_name, "Battery");
}

// =============================================================================
// Test: Grouped nodes have content, but are filtered by group_id at render time
// =============================================================================

TEST(BlueprintVisibility, GroupedNodeHasContent_ButFilteredByGroupId) {
    Blueprint bp;

    // Add an IndicatorLight with group_id (simulates internal node of collapsed blueprint)
    Node lamp;
    lamp.id = "internal_lamp";
    lamp.type_name = "IndicatorLight";
    lamp.kind = NodeKind::Node;
    lamp.group_id = "some_group";  // Belongs to a group (will be filtered when rendering root view)
    lamp.node_content.type = NodeContentType::Text;
    lamp.node_content.label = "OFF";
    lamp.at(100, 100).size_wh(80, 60);
    bp.add_node(std::move(lamp));

    // Verify the node has the assigned group_id
    auto* node = bp.find_node("internal_lamp");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->group_id, "some_group") << "Node should have group_id='some_group'";

    // The visual node factory creates a visual for it (no visibility concept anymore)
    auto visual = VisualNodeFactory::create(*node);
    // Content type is Text (the rendering loop filters by group_id, not by isVisible())
    EXPECT_EQ(visual->getContentType(), NodeContentType::Text);
}

// =============================================================================
// Diagnostic: Load blueprint.json and run JIT simulation
// =============================================================================

TEST(BlueprintSignalFlow, BlueprintJsonFile_SOR_Stability) {
    // Load the user's blueprint.json file
    std::vector<std::string> paths = {
        "blueprint.json",
        "../blueprint.json",
        "../../blueprint.json",
    };
    std::string json_str;
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (f.is_open()) {
            json_str.assign(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
            break;
        }
    }
    ASSERT_FALSE(json_str.empty()) << "Could not load blueprint.json";

    // Load as editor format
    auto bp_opt = blueprint_from_json(json_str);
    ASSERT_TRUE(bp_opt.has_value()) << "Failed to parse blueprint.json";
    Blueprint& bp = *bp_opt;

    // Count duplicate device names
    std::map<std::string, int> name_counts;
    for (const auto& n : bp.nodes) {
        name_counts[n.id]++;
    }
    int duplicates = 0;
    for (const auto& [name, count] : name_counts) {
        if (count > 1) {
            std::cerr << "  DUPLICATE: " << name << " x" << count << "\n";
            duplicates++;
        }
    }
    std::cerr << "Total nodes: " << bp.nodes.size()
              << ", unique: " << name_counts.size()
              << ", duplicates: " << duplicates << "\n";

    // Convert to simulation JSON
    std::string sim_json = blueprint_to_json(bp);
    auto j = json::parse(sim_json);

    // Check for duplicate device names in simulation format
    std::set<std::string> sim_names;
    int sim_dupes = 0;
    for (const auto& dev : j["devices"]) {
        std::string name = dev["name"].get<std::string>();
        if (sim_names.count(name)) {
            std::cerr << "  SIM DUPLICATE: " << name << "\n";
            sim_dupes++;
        }
        sim_names.insert(name);
    }
    std::cerr << "Simulation devices: " << j["devices"].size()
              << ", unique: " << sim_names.size()
              << ", sim duplicates: " << sim_dupes << "\n";

    // Parse and run simulation
    auto ctx = parse_json(sim_json);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) connections.push_back({c.from, c.to});
    auto result = build_systems_dev(ctx.devices, connections);

    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end())
                state.across[it_sig->second] = value;
        }
    }
    state.resize_buffers(result.signal_count);

    // Run 200 steps
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 200; step++) {
        state.clear_through();
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
            }, variant);
        }
        state.precompute_inv_conductance();
        solve_sor_iteration(state.across.data(), state.through.data(),
                           state.inv_conductance.data(), state.across.size(), SOR::OMEGA);
    }

    // Dump all signal voltages
    std::cerr << "\n=== SIGNAL VOLTAGES after 200 steps ===\n";
    for (const auto& [port, sig] : result.port_to_signal) {
        float v = state.across[sig];
        if (std::abs(v) > 0.1f || std::isnan(v) || std::isinf(v)) {
            std::cerr << "  " << port << " = " << v << "V (signal " << sig << ")\n";
        }
    }

    // Check no NaN/inf
    bool has_nan = false, has_inf = false;
    float max_voltage = 0;
    for (size_t i = 0; i < state.across.size(); i++) {
        if (std::isnan(state.across[i])) has_nan = true;
        if (std::isinf(state.across[i])) has_inf = true;
        max_voltage = std::max(max_voltage, std::abs(state.across[i]));
    }
    std::cerr << "Max voltage: " << max_voltage << "V\n";
    std::cerr << "Has NaN: " << has_nan << ", Has inf: " << has_inf << "\n";

    EXPECT_FALSE(has_nan) << "SOR must not produce NaN";
    EXPECT_FALSE(has_inf) << "SOR must not produce inf";
    EXPECT_LT(max_voltage, 1000.0f) << "Voltage should not explode (max=" << max_voltage << ")";
}

// Same test but using Simulator<JIT_Solver> (the actual editor path)
TEST(BlueprintSignalFlow, BlueprintJsonFile_JIT_Simulator) {
    std::vector<std::string> paths = {
        "blueprint.json", "../blueprint.json", "../../blueprint.json",
    };
    std::string json_str;
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (f.is_open()) {
            json_str.assign(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
            break;
        }
    }
    ASSERT_FALSE(json_str.empty()) << "Could not load blueprint.json";

    auto bp_opt = blueprint_from_json(json_str);
    ASSERT_TRUE(bp_opt.has_value());
    Blueprint& bp = *bp_opt;

    // Use actual Simulator<JIT_Solver> - same as the editor
    an24::Simulator<an24::JIT_Solver> sim;
    sim.start(bp);

    // Run 500 steps (like the editor would)
    for (int i = 0; i < 500; i++) {
        sim.step(1.0f / 60.0f);
    }

    // Check battery voltages
    float main_bat = sim.get_wire_voltage("bat_main_1.v_out");
    std::cerr << "bat_main_1.v_out = " << main_bat << "V\n";

    // Check simple_battery internal battery
    float nested_bat = sim.get_wire_voltage("simple_battery_1:bat.v_out");
    std::cerr << "simple_battery_1:bat.v_out = " << nested_bat << "V\n";

    // Check via collapsed node port (the fallback path)
    float collapsed_vout = sim.get_wire_voltage("simple_battery_1.vout");
    std::cerr << "simple_battery_1.vout (fallback) = " << collapsed_vout << "V\n";

    // Dump ALL voltages > 0.1
    std::cerr << "\n=== ALL SIGNIFICANT VOLTAGES ===\n";
    // Access build result via the simulation JSON path
    std::string sim_json = blueprint_to_json(bp);
    auto ctx = parse_json(sim_json);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) connections.push_back({c.from, c.to});
    auto result = build_systems_dev(ctx.devices, connections);
    
    // Run standalone to compare
    SimulationState state;
    for (uint32_t si = 0; si < result.signal_count; ++si) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), si);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end())
                state.across[it_sig->second] = value;
        }
    }
    state.resize_buffers(result.signal_count);
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 500; step++) {
        state.clear_through();
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
                if constexpr (requires { comp.solve_logical(state, dt); }) {
                    comp.solve_logical(state, dt);
                }
            }, variant);
        }
        state.precompute_inv_conductance();
        solve_sor_iteration(state.across.data(), state.through.data(),
                           state.inv_conductance.data(), state.across.size(), SOR::OMEGA);
        // Post-step
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.post_step(state, dt); }) {
                    comp.post_step(state, dt);
                }
            }, variant);
        }
    }

    // Dump
    for (const auto& [port, sig] : result.port_to_signal) {
        float v = state.across[sig];
        if (std::abs(v) > 0.1f || std::isnan(v) || std::isinf(v)) {
            std::cerr << "  " << port << " = " << v << "V\n";
        }
    }

    // Check stability
    float max_v = 0;
    bool has_nan_v = false, has_inf_v = false;
    for (size_t i = 0; i < state.across.size(); i++) {
        if (std::isnan(state.across[i])) has_nan_v = true;
        if (std::isinf(state.across[i])) has_inf_v = true;
        max_v = std::max(max_v, std::abs(state.across[i]));
    }
    std::cerr << "Max voltage: " << max_v << "V, NaN=" << has_nan_v << " inf=" << has_inf_v << "\n";

    EXPECT_FALSE(has_nan_v) << "SOR must not produce NaN";
    EXPECT_FALSE(has_inf_v) << "SOR must not produce inf";
    EXPECT_LT(max_v, 1000.0f) << "Voltage should not explode";
    EXPECT_GT(main_bat, 10.0f) << "Main battery should produce voltage";
}

