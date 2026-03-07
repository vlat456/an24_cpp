#include <gtest/gtest.h>
#include "editor/render.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport/viewport.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
using namespace an24;

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
    n.collapsed = true;  // FAIL: No such field
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);

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

    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);

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

    n.at(100.0f, 50.0f);
    n.size_wh(140.0f, 80.0f);

    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Check for specific port type colors
    // Colors are ImGui 32-bit RGBA: 0xRRGGBBAA
    EXPECT_TRUE(dl.has_circle_with_color(0xFF0000FF)) << "V port should be red";
    EXPECT_TRUE(dl.has_circle_with_color(0x0080FFFF)) << "I port should be blue";
    EXPECT_TRUE(dl.has_circle_with_color(0x00FF00FF)) << "Bool port should be green";
    EXPECT_TRUE(dl.has_circle_with_color(0xFF8000FF)) << "RPM port should be orange";
}

TEST(CollapsedNode, VisualIndicator_IconOrBadge) {
    // FAIL: Need visual indicator for collapsible nodes
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "bat1";
    n.type_name = "simple_battery";
    n.collapsed = true;
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);

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

    // FAIL: Need logic to populate inputs/outputs from exposed_ports
    // This is what we'll implement:
    // for (const auto& [name, port] : exposed_ports) {
    //     if (port.direction == PortDirection::In) {
    //         node.inputs.push_back(Port(name.c_str(), PortSide::Input, port.type));
    //     } else {
    //         node.outputs.push_back(Port(name.c_str(), PortSide::Output, port.type));
    //     }
    // }

    // After implementation, these should pass:
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
    regular.at(50.0f, 50.0f);
    regular.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(regular));

    // Collapsed nested blueprint node
    Node collapsed;
    collapsed.id = "bat1";
    collapsed.name = "bat1";
    collapsed.type_name = "simple_battery";
    collapsed.collapsed = true;
    collapsed.at(200.0f, 50.0f);
    collapsed.size_wh(120.0f, 80.0f);
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
    // FAIL: Need to persist collapsed state
    // This is for Phase 5.3, keeping as placeholder

    // TODO: Serialize collapsed field to JSON
    // TODO: Serialize blueprint_path to JSON

    SUCCEED() << "Placeholder for persistence tests";
}

TEST(Persistence, CollapsedState_LoadsFromJson) {
    // FAIL: Need to load collapsed state from JSON
    // This is for Phase 5.3, keeping as placeholder

    // TODO: Deserialize collapsed field from JSON
    // TODO: Deserialize blueprint_path from JSON

    SUCCEED() << "Placeholder for persistence tests";
}
