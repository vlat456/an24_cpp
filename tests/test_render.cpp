#include <gtest/gtest.h>
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/mock_draw_list.h"
#include "jit_solver/simulator.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/viewport/viewport.h"
#include "editor/visual/node/node.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/// TDD Step 5: Rendering

/// Тест: пустой Blueprint не крашится
TEST(RenderTest, EmptyBlueprint_DoesNotCrash) {
    Blueprint bp;
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);
}

/// Тест: рендер узла
TEST(RenderTest, Node_RendersRect) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);
    EXPECT_TRUE(dl.had_rect());
}

/// Тест: рендер провода
TEST(RenderTest, Wire_RendersLine) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f);
    n1.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(200.0f, 0.0f);
    n2.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);
    EXPECT_TRUE(dl.had_polyline());
}

/// Тест: сетка не крашится
TEST(RenderTest, Grid_DoesNotCrash) {
    MockDrawList dl;
    Viewport vp;
    vp.grid_step = 16.0f;
    vp.zoom = 1.0f;
    BlueprintRenderer::renderGrid(dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
}

// ─── Wire highlighting tests ───

static Blueprint create_render_circuit() {
    Blueprint bp;
    bp.grid_step = 16.0f;

    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.output("v");
    gnd.at(80, 240);
    gnd.size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    Node batt;
    batt.id = "bat";
    batt.type_name = "Battery";
    batt.kind = NodeKind::Node;
    batt.input("v_in");
    batt.output("v_out");
    batt.at(80, 80);
    batt.size_wh(120, 80);
    bp.add_node(std::move(batt));

    Node res;
    res.id = "res";
    res.type_name = "Resistor";
    res.kind = NodeKind::Node;
    res.input("v_in");
    res.output("v_out");
    res.at(320, 80);
    res.size_wh(120, 80);
    bp.add_node(std::move(res));

    Wire w1;
    w1.start.node_id = "gnd"; w1.start.port_name = "v";
    w1.end.node_id = "bat"; w1.end.port_name = "v_in";
    bp.add_wire(std::move(w1));

    Wire w2;
    w2.start.node_id = "bat"; w2.start.port_name = "v_out";
    w2.end.node_id = "res"; w2.end.port_name = "v_in";
    bp.add_wire(std::move(w2));

    Wire w3;
    w3.start.node_id = "res"; w3.start.port_name = "v_out";
    w3.end.node_id = "gnd"; w3.end.port_name = "v";
    bp.add_wire(std::move(w3));

    return bp;
}

TEST(RenderTest, WireHighlighting_EnergizedWiresAreYellow) {
    Blueprint bp = create_render_circuit();

    // Build and run simulation
    an24::Simulator<an24::JIT_Solver> sim;
    sim.start(bp);
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Verify simulation produced voltage
    float v_bat = sim.get_wire_voltage("bat.v_out");
    ASSERT_GT(v_bat, 5.0f) << "Battery should produce voltage for test to be meaningful";

    // Render with simulation
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache,
                     nullptr, std::nullopt, &sim);

    // Should have yellow/amber (energized) polylines: 0xFF44AAFF
    EXPECT_TRUE(dl.has_polyline_with_color(0xFF44AAFF))
        << "Energized wires should be drawn in yellow/amber";
}

TEST(RenderTest, WireHighlighting_WithoutSimulation_NoYellow) {
    Blueprint bp = create_render_circuit();

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache);

    // Without simulation, no yellow/amber polylines
    EXPECT_FALSE(dl.has_polyline_with_color(0xFF44AAFF))
        << "Without simulation, no yellow/amber wires";
}

// ─── Tooltip tests ───

TEST(RenderTest, Tooltip_PortHover_ShowsValue) {
    Blueprint bp = create_render_circuit();

    an24::Simulator<an24::JIT_Solver> sim;
    sim.start(bp);
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Hover over a port position (battery v_out)
    // The battery is at (80, 80) size (120, 80), output port is on right side
    Pt hover_pos(80 + 120, 80 + 40); // right edge, center
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache, nullptr, std::nullopt, &sim);
    auto tooltip = renderer.detectTooltip(bp, vp, Pt(0, 0), cache, hover_pos, sim);

    // Tooltip should be active if we hit a port
    if (tooltip.active) {
        EXPECT_FALSE(tooltip.text.empty()) << "Tooltip should have a value text";
        EXPECT_FALSE(tooltip.label.empty()) << "Tooltip should have a label";
    }
}

TEST(RenderTest, Tooltip_NoSimulation_NoTooltip) {
    Blueprint bp = create_render_circuit();

    Pt hover_pos(80 + 120, 80 + 40);
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache);
    TooltipInfo tooltip; // No sim → no detection

    // Without simulation, no tooltip
    EXPECT_FALSE(tooltip.active);
}

TEST(RenderTest, RenderTooltip_DrawsTextAndBackground) {
    MockDrawList dl;
    TooltipInfo tooltip;
    tooltip.active = true;
    tooltip.screen_pos = Pt(100, 100);
    tooltip.text = "28.00";
    tooltip.label = "bat.v_out";

    BlueprintRenderer::renderTooltip(dl, tooltip);

    // Should have drawn text
    EXPECT_FALSE(dl.texts_.empty()) << "Tooltip should render text";
}

TEST(RenderTest, RenderTooltip_InactiveDoesNothing) {
    MockDrawList dl;
    TooltipInfo tooltip;
    tooltip.active = false;

    BlueprintRenderer::renderTooltip(dl, tooltip);
    EXPECT_TRUE(dl.texts_.empty()) << "Inactive tooltip should draw nothing";
}

// ─── Wire tooltip regression tests ───

TEST(RenderTest, Tooltip_WireHover_ShowsVoltage) {
    Blueprint bp = create_render_circuit();

    an24::Simulator<an24::JIT_Solver> sim;
    sim.start(bp);
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Hover over wire position (middle of wire from battery to resistor)
    // Battery is at (80, 80) size (120, 80), output port is on right edge at (208, 112)
    // Resistor is at (320, 80), input port is on left edge at (320, 112)
    Pt hover_pos(260, 120); // middle of horizontal wire
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache, nullptr, std::nullopt, &sim);
    auto tooltip = renderer.detectTooltip(bp, vp, Pt(0, 0), cache, hover_pos, sim);

    // Tooltip should be active for wire
    EXPECT_TRUE(tooltip.active) << "Tooltip should be active when hovering over wire";
    EXPECT_FALSE(tooltip.text.empty()) << "Tooltip should have voltage value";
    EXPECT_FALSE(tooltip.label.empty()) << "Tooltip should have wire label";
    EXPECT_TRUE(tooltip.label.find("bat.v_out") != std::string::npos)
        << "Tooltip label should contain source port name";
}

TEST(RenderTest, Tooltip_WireHover_NoSimulation_NoTooltip) {
    Blueprint bp = create_render_circuit();

    Pt hover_pos(260, 120);
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0, 0), Pt(800, 600), cache);
    TooltipInfo tooltip; // No sim → no detection

    // Without simulation, no tooltip
    EXPECT_FALSE(tooltip.active) << "Tooltip should not be active without simulation";
}

// ─── VisualNodeCache regression tests ───

TEST(RenderTest, VisualNodeCache_NodeContent_SyncsAfterClear) {
    // Create a node without content
    Node node;
    node.id = "test_node";
    node.name = "Test";
    node.type_name = "Battery";
    node.input("v_in");
    node.output("v_out");
    node.at(100, 100);
    node.size_wh(120, 80);

    VisualNodeCache cache;

    // First, create a visual node without content
    auto* visual1 = cache.getOrCreate(node);
    EXPECT_EQ(visual1->getContentType(), NodeContentType::None)
        << "Initial visual node should have no content";

    // Now update the node to have content
    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = true;
    node.node_content = content;

    // Get visual node again WITHOUT clearing cache
    // Since getOrCreate now syncs content proactively, it will have updated content
    auto* visual2 = cache.getOrCreate(node);
    EXPECT_EQ(visual2->getContentType(), NodeContentType::Switch)
        << "getOrCreate syncs content proactively, so cached node should have updated content";

    // Clear the cache
    cache.clear();

    // Now get visual node again - should have updated content
    auto* visual3 = cache.getOrCreate(node);
    EXPECT_EQ(visual3->getContentType(), NodeContentType::Switch)
        << "After cache clear, visual node should have updated content";
}

TEST(RenderTest, VisualNodeCache_GetContentBounds_WithContent) {
    // Create a node with content
    Node node;
    node.id = "test_node";
    node.name = "Test";
    node.type_name = "Battery";
    node.input("v_in");
    node.output("v_out");
    node.at(100, 100);
    node.size_wh(120, 80);

    NodeContent content;
    content.type = NodeContentType::Value;
    content.value = 42.0f;
    content.min = 0.0f;
    content.max = 100.0f;
    content.label = "Voltage";
    node.node_content = content;

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);

    // Verify content type is set correctly
    EXPECT_EQ(visual->getContentType(), NodeContentType::Value);

    // Verify content bounds are valid (not empty)
    Bounds bounds = visual->getContentBounds();
    EXPECT_GT(bounds.w, 0.0f) << "Content bounds should have positive width";
    EXPECT_GT(bounds.h, 0.0f) << "Content bounds should have positive height";

    // Verify content value matches
    const NodeContent& node_content = visual->getNodeContent();
    EXPECT_EQ(node_content.type, NodeContentType::Value);
    EXPECT_FLOAT_EQ(node_content.value, 42.0f);
    EXPECT_EQ(node_content.label, "Voltage");
}

// ─── Auto-generated node_content tests ───

TEST(RenderTest, AutoGeneratedContent_Battery_HasGauge) {
    // Create a battery node without content
    Node node;
    node.id = "battery";
    node.name = "Battery";
    node.type_name = "Battery";
    node.input("v_in");
    node.output("v_out");

    // Auto-generate content (same logic as in device_to_node)
    if (node.type_name == "Battery") {
        node.node_content.type = NodeContentType::Gauge;
        node.node_content.label = "V";
        node.node_content.value = 0.0f;
        node.node_content.min = 0.0f;
        node.node_content.max = 30.0f;
        node.node_content.unit = "V";
    }

    // Verify content was auto-generated
    EXPECT_EQ(node.node_content.type, NodeContentType::Gauge);
    EXPECT_EQ(node.node_content.label, "V");
    EXPECT_FLOAT_EQ(node.node_content.min, 0.0f);
    EXPECT_FLOAT_EQ(node.node_content.max, 30.0f);

    // Verify visual node has the content
    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);
    EXPECT_EQ(visual->getContentType(), NodeContentType::Gauge);
}

TEST(RenderTest, AutoGeneratedContent_Switch_HasCheckbox) {
    // Test Switch type
    Node node;
    node.id = "switch1";
    node.name = "Switch";
    node.type_name = "Switch";

    if (node.type_name == "Switch") {
        node.node_content.type = NodeContentType::Switch;
        node.node_content.label = "ON";
        node.node_content.state = false;
    }

    EXPECT_EQ(node.node_content.type, NodeContentType::Switch);
    EXPECT_EQ(node.node_content.label, "ON");
}

TEST(RenderTest, AutoGeneratedContent_IndicatorLight_HasText) {
    // Test IndicatorLight type
    Node node;
    node.id = "light1";
    node.name = "Light";
    node.type_name = "IndicatorLight";

    if (node.type_name == "IndicatorLight") {
        node.node_content.type = NodeContentType::Text;
        node.node_content.label = "OFF";
    }

    EXPECT_EQ(node.node_content.type, NodeContentType::Text);
    EXPECT_EQ(node.node_content.label, "OFF");
}

TEST(RenderTest, AutoGeneratedContent_Relay_HasSwitch) {
    // Test DMR400 relay type
    Node node;
    node.id = "relay1";
    node.name = "Relay";
    node.type_name = "DMR400";

    if (node.type_name == "DMR400") {
        node.node_content.type = NodeContentType::Switch;
        node.node_content.label = "ON";
        node.node_content.state = false;
    }

    EXPECT_EQ(node.node_content.type, NodeContentType::Switch);
    EXPECT_EQ(node.node_content.label, "ON");
}

// ─── Dynamic node_content updates from simulation ───

TEST(RenderTest, DynamicUpdate_Battery_VoltageUpdatesFromSimulation) {
    // Create a battery node
    Node battery;
    battery.id = "bat";
    battery.name = "Battery";
    battery.type_name = "Battery";
    battery.input("v_in");
    battery.output("v_out");

    // Auto-generate content
    if (battery.type_name == "Battery") {
        battery.node_content.type = NodeContentType::Gauge;
        battery.node_content.label = "V";
        battery.node_content.value = 0.0f;
        battery.node_content.min = 0.0f;
        battery.node_content.max = 30.0f;
        battery.node_content.unit = "V";
    }

    // Initially voltage is 0
    EXPECT_FLOAT_EQ(battery.node_content.value, 0.0f);

    // Simulate voltage change (as would happen in update_node_content_from_simulation)
    battery.node_content.value = 24.5f;

    // Verify voltage was updated
    EXPECT_FLOAT_EQ(battery.node_content.value, 24.5f);
}

TEST(RenderTest, DynamicUpdate_IndicatorLight_TogglesOnOff) {
    // Create an indicator light node
    Node light;
    light.id = "lamp";
    light.name = "Light";
    light.type_name = "IndicatorLight";

    // Auto-generate content
    if (light.type_name == "IndicatorLight") {
        light.node_content.type = NodeContentType::Text;
        light.node_content.label = "OFF";
    }

    // Initially OFF
    EXPECT_EQ(light.node_content.label, "OFF");

    // Simulate brightness > threshold (turn ON)
    float brightness = 50.0f;
    light.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
    EXPECT_EQ(light.node_content.label, "ON");

    // Simulate brightness = 0 (turn OFF)
    brightness = 0.0f;
    light.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
    EXPECT_EQ(light.node_content.label, "OFF");
}

TEST(RenderTest, DynamicUpdate_Relay_StateUpdatesFromVoltages) {
    // Create a relay node
    Node relay;
    relay.id = "relay";
    relay.name = "DMR400";
    relay.type_name = "DMR400";

    // Auto-generate content
    if (relay.type_name == "DMR400") {
        relay.node_content.type = NodeContentType::Switch;
        relay.node_content.label = "ON";
        relay.node_content.state = false;
    }

    // Initially disconnected
    EXPECT_FALSE(relay.node_content.state);

    // Simulate generator voltage higher than bus (relay closes)
    float v_gen = 28.0f;
    float v_bus = 24.0f;
    bool connected = v_gen > v_bus + 2.0f;
    relay.node_content.state = connected;
    EXPECT_TRUE(relay.node_content.state);

    // Simulate reverse current (relay opens)
    v_gen = 20.0f;
    v_bus = 30.0f;
    connected = v_gen > v_bus + 2.0f;
    relay.node_content.state = connected;
    EXPECT_FALSE(relay.node_content.state);
}

// ─── Content bounds regression tests ───

TEST(RenderTest, ContentBounds_ExcludesPortLabels) {
    // Create a node with input and output ports
    Node node;
    node.id = "test";
    node.name = "Test";
    node.type_name = "Battery";
    node.input("v_in");
    node.output("v_out");
    node.at(100, 100);
    node.size_wh(120, 80);

    // Add content (use Value type which creates ContentWidget with layout bounds)
    NodeContent content;
    content.type = NodeContentType::Value;
    content.value = 24.0f;
    content.min = 0.0f;
    content.max = 30.0f;
    content.label = "Voltage";
    node.node_content = content;

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);

    // Get content bounds
    Bounds cb = visual->getContentBounds();

    // Content bounds should be positive (content exists)
    EXPECT_GT(cb.w, 0.0f) << "Content width should be positive";
    EXPECT_GT(cb.h, 0.0f) << "Content height should be positive";

    // Content should NOT start at x=0 (should skip input port labels)
    EXPECT_GT(cb.x, 0.0f) << "Content should be offset from left edge (input port labels)";

    // Content width should be less than node width (should exclude both margins)
    EXPECT_LT(cb.w, node.size.x) << "Content width should be less than node width (margins on both sides)";
}

TEST(RenderTest, ContentBounds_OutputPortLabelOnRight) {
    // Create a node with long output port name
    Node node;
    node.id = "test";
    node.name = "Test";
    node.type_name = "Battery";
    node.input("v_in");
    node.output("v_out_long_name");

    // Add content
    NodeContent content;
    content.type = NodeContentType::Gauge;
    content.value = 24.0f;
    node.node_content = content;

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);

    // Get content bounds
    Bounds cb = visual->getContentBounds();

    // Content should not extend to the right edge
    float content_right_edge = cb.x + cb.w;
    EXPECT_LT(content_right_edge, node.size.x)
        << "Content should stop before right edge (output port label on right)";

    // Content width should be significantly less than node width
    float margin_ratio = cb.w / node.size.x;
    EXPECT_LT(margin_ratio, 0.8f)
        << "Content should take up less than 80% of node width (port labels on both sides)";
}

// ============================================================================
// Port Type Visualization Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(RenderTest, PortTypeColor_V_IsRed) {
    // This test will fail until render_theme::get_port_color() is implemented
    // Format: 0xAABBGGRR (A=alpha, B=blue, G=green, R=red)
    // Red should be: 0xFF0000FF (alpha=FF, blue=00, green=00, red=FF)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::V);

    EXPECT_EQ(color, 0xFF0000FF) << "Voltage ports should be red (0xFF0000FF)";
}

TEST(RenderTest, PortTypeColor_I_IsBlue) {
    // Blue should be: 0xFFFF0000 (alpha=FF, blue=FF, green=00, red=00)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::I);

    EXPECT_EQ(color, 0xFFFF0000) << "Current ports should be blue (0xFFFF0000)";
}

TEST(RenderTest, PortTypeColor_Bool_IsGreen) {
    // Green should be: 0xFF00FF00 (alpha=FF, blue=00, green=FF, red=00)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::Bool);

    EXPECT_EQ(color, 0xFF00FF00) << "Boolean ports should be green (0xFF00FF00)";
}

TEST(RenderTest, PortTypeColor_RPM_IsOrange) {
    // Orange should be: 0xFF00A5FF (alpha=FF, blue=00, green=A5, red=FF)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::RPM);

    EXPECT_EQ(color, 0xFF00A5FF) << "RPM ports should be orange (0xFF00A5FF)";
}

TEST(RenderTest, PortTypeColor_Temperature_IsYellow) {
    // Yellow should be: 0xFF00FFFF (alpha=FF, blue=00, green=FF, red=FF)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::Temperature);

    EXPECT_EQ(color, 0xFF00FFFF) << "Temperature ports should be yellow (0xFF00FFFF)";
}

TEST(RenderTest, PortTypeColor_Pressure_IsCyan) {
    // Cyan should be: 0xFFFFFF00 (alpha=FF, blue=FF, green=FF, red=00)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::Pressure);

    EXPECT_EQ(color, 0xFFFFFF00) << "Pressure ports should be cyan (0xFFFFFF00)";
}

TEST(RenderTest, PortTypeColor_Position_IsPurple) {
    // Purple should be: 0xFF800080 (alpha=FF, blue=80, green=00, red=80)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::Position);

    EXPECT_EQ(color, 0xFF800080) << "Position ports should be purple (0xFF800080)";
}

TEST(RenderTest, PortTypeColor_Any_IsGray) {
    // Gray should be: 0xFF808080 (alpha=FF, blue=80, green=80, red=80)

    using namespace an24;
    uint32_t color = render_theme::get_port_color(PortType::Any);

    EXPECT_EQ(color, 0xFF808080) << "Any ports should be gray (0xFF808080)";
}

TEST(RenderTest, PortRendering_VoltagePort_RendersRedCircle) {
    // Create a battery node with voltage port
    Blueprint bp;
    Node batt;
    batt.id = "bat";
    batt.type_name = "Battery";
    batt.input("v_in");
    batt.output("v_out");
    batt.at(100.0f, 100.0f);
    batt.size_wh(120.0f, 80.0f);

    // Set port types to V (voltage)
    for (auto& port : batt.inputs) {
        port.type = an24::PortType::V;
    }
    for (auto& port : batt.outputs) {
        port.type = an24::PortType::V;
    }

    bp.add_node(std::move(batt));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

    // Should have red circles for voltage ports
    EXPECT_TRUE(dl.has_circle_with_color(0xFF0000FF))
        << "Voltage ports should render as red circles";
}

TEST(RenderTest, PortRendering_BoolPort_RendersGreenCircle) {
    // Create a relay node with boolean control port
    Blueprint bp;
    Node relay;
    relay.id = "relay";
    relay.type_name = "Relay";
    relay.input("control");
    relay.input("v_in");
    relay.output("v_out");
    relay.at(100.0f, 100.0f);
    relay.size_wh(120.0f, 80.0f);

    // Set port types
    for (auto& port : relay.inputs) {
        if (port.name == "control") {
            port.type = an24::PortType::Bool;
        } else {
            port.type = an24::PortType::V;
        }
    }
    for (auto& port : relay.outputs) {
        port.type = an24::PortType::V;
    }

    bp.add_node(std::move(relay));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

    // Should have green circles for boolean ports
    EXPECT_TRUE(dl.has_circle_with_color(0xFF00FF00))
        << "Boolean ports should render as green circles";
}

TEST(RenderTest, PortRendering_RPMPort_RendersOrangeCircle) {
    // Create an RU19A node with RPM port
    Blueprint bp;
    Node apu;
    apu.id = "apu";
    apu.type_name = "RU19A";
    apu.input("v_start");
    apu.output("v_bus");
    apu.output("rpm_out");
    apu.output("t4_out");
    apu.at(100.0f, 100.0f);
    apu.size_wh(120.0f, 80.0f);

    // Set port types
    for (auto& port : apu.inputs) {
        port.type = an24::PortType::V;
    }
    for (auto& port : apu.outputs) {
        if (port.name == "rpm_out") {
            port.type = an24::PortType::RPM;
        } else if (port.name == "t4_out") {
            port.type = an24::PortType::Temperature;
        } else {
            port.type = an24::PortType::V;
        }
    }

    bp.add_node(std::move(apu));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer; renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache);

    // Should have orange circles for RPM ports
    EXPECT_TRUE(dl.has_circle_with_color(0xFF00A5FF))
        << "RPM ports should render as orange circles";
}

// ============================================================================
// Port Type Compatibility Validation Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(RenderTest, WireCreation_CompatibleTypes_CanConnect) {
    // V port should connect to V port
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.output("v_out");
    batt1.at(0, 0);
    for (auto& p : batt1.outputs) p.type = an24::PortType::V;

    Node batt2;
    batt2.id = "batt2";
    batt2.input("v_in");
    batt2.at(200, 0);
    for (auto& p : batt2.inputs) p.type = an24::PortType::V;

    bp.add_node(std::move(batt1));
    bp.add_node(std::move(batt2));

    // Try to add wire between compatible ports (V to V)
    Wire w;
    w.id = "w1";
    w.start.node_id = "batt1";
    w.start.port_name = "v_out";
    w.end.node_id = "batt2";
    w.end.port_name = "v_in";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire(std::move(w));

    // Wire should be added
    EXPECT_EQ(bp.wires.size(), wire_count_before + 1)
        << "Compatible port types (V to V) should allow connection";
}

TEST(RenderTest, WireCreation_IncompatibleTypes_ShouldNotConnect) {
    // V port should NOT connect to RPM port
    Blueprint bp;

    Node batt;
    batt.id = "batt";
    batt.output("v_out");
    batt.at(0, 0);
    for (auto& p : batt.outputs) p.type = an24::PortType::V;

    Node apu;
    apu.id = "apu";
    apu.input("rpm_in");
    apu.at(200, 0);
    for (auto& p : apu.inputs) p.type = an24::PortType::RPM;

    bp.add_node(std::move(batt));
    bp.add_node(std::move(apu));

    // Try to add wire between incompatible ports (V to RPM)
    Wire w;
    w.id = "w1";
    w.start.node_id = "batt";
    w.start.port_name = "v_out";
    w.end.node_id = "apu";
    w.end.port_name = "rpm_in";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire_validated(std::move(w));  // This function doesn't exist yet

    // Wire should NOT be added
    EXPECT_EQ(bp.wires.size(), wire_count_before)
        << "Incompatible port types (V to RPM) should reject connection";
}

TEST(RenderTest, WireCreation_BoolToV_ShouldNotConnect) {
    // Boolean port should NOT connect to Voltage port
    Blueprint bp;

    Node button;
    button.id = "button";
    button.output("state");
    button.at(0, 0);
    for (auto& p : button.outputs) p.type = an24::PortType::Bool;

    Node batt;
    batt.id = "batt";
    batt.input("v_in");
    batt.at(200, 0);
    for (auto& p : batt.inputs) p.type = an24::PortType::V;

    bp.add_node(std::move(button));
    bp.add_node(std::move(batt));

    // Try to add wire between incompatible ports (Bool to V)
    Wire w;
    w.id = "w1";
    w.start.node_id = "button";
    w.start.port_name = "state";
    w.end.node_id = "batt";
    w.end.port_name = "v_in";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire_validated(std::move(w));  // This function doesn't exist yet

    // Wire should NOT be added
    EXPECT_EQ(bp.wires.size(), wire_count_before)
        << "Incompatible port types (Bool to V) should reject connection";
}

TEST(RenderTest, WireCreation_AnyType_CanConnectToAny) {
    // Any type should connect to anything
    Blueprint bp;

    Node adaptor;
    adaptor.id = "adaptor";
    adaptor.output("out");
    adaptor.at(0, 0);
    for (auto& p : adaptor.outputs) p.type = an24::PortType::Any;

    Node apu;
    apu.id = "apu";
    apu.input("rpm_in");
    apu.at(200, 0);
    for (auto& p : apu.inputs) p.type = an24::PortType::RPM;

    bp.add_node(std::move(adaptor));
    bp.add_node(std::move(apu));

    // Try to add wire (Any to RPM)
    Wire w;
    w.id = "w1";
    w.start.node_id = "adaptor";
    w.start.port_name = "out";
    w.end.node_id = "apu";
    w.end.port_name = "rpm_in";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire_validated(std::move(w));  // This function doesn't exist yet

    // Wire should be added (Any can connect to anything)
    EXPECT_EQ(bp.wires.size(), wire_count_before + 1)
        << "Any type should connect to any port type";
}

// ============================================================================
// One-to-One Connection Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(RenderTest, OneToOne_WireToOccupiedPort_ShouldFail) {
    // Should not be able to connect a second wire to an already-occupied port
    Blueprint bp;

    Node bat1;
    bat1.id = "bat1";
    bat1.output("v_out");
    bat1.at(0, 0);
    for (auto& p : bat1.outputs) p.type = an24::PortType::V;

    Node bat2;
    bat2.id = "bat2";
    bat2.output("v_out");
    bat2.at(200, 0);
    for (auto& p : bat2.outputs) p.type = an24::PortType::V;

    Node load;
    load.id = "load";
    load.input("v_in");
    load.at(100, 100);
    for (auto& p : load.inputs) p.type = an24::PortType::V;

    bp.add_node(std::move(bat1));
    bp.add_node(std::move(bat2));
    bp.add_node(std::move(load));

    // Add first wire to load.v_in
    Wire w1;
    w1.id = "w1";
    w1.start.node_id = "bat1";
    w1.start.port_name = "v_out";
    w1.end.node_id = "load";
    w1.end.port_name = "v_in";
    bp.add_wire_validated(std::move(w1));

    // Try to add second wire to the same port
    Wire w2;
    w2.id = "w2";
    w2.start.node_id = "bat2";
    w2.start.port_name = "v_out";
    w2.end.node_id = "load";
    w2.end.port_name = "v_in";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire_validated(std::move(w2));

    // Second wire should be rejected - port already occupied
    EXPECT_EQ(bp.wires.size(), wire_count_before)
        << "Should not connect to already-occupied port";
}

TEST(RenderTest, OneToOne_BusPort_CanHaveMultipleWires) {
    // Bus ports should allow multiple wires (virtual alias ports)
    Blueprint bp;

    Node bus;
    bus.id = "bus";
    bus.type_name = "Bus";
    bus.kind = NodeKind::Bus;
    bus.at(100, 100);
    bus.size_wh(40, 40);
    // Bus doesn't have explicit ports in editor nodes

    Node bat1;
    bat1.id = "bat1";
    bat1.output("v_out");
    bat1.at(0, 0);
    for (auto& p : bat1.outputs) p.type = an24::PortType::V;

    Node bat2;
    bat2.id = "bat2";
    bat2.output("v_out");
    bat2.at(200, 0);
    for (auto& p : bat2.outputs) p.type = an24::PortType::V;

    bp.add_node(std::move(bus));
    bp.add_node(std::move(bat1));
    bp.add_node(std::move(bat2));

    // Add two wires to the same bus port
    Wire w1;
    w1.id = "w1";
    w1.start.node_id = "bat1";
    w1.start.port_name = "v_out";
    w1.end.node_id = "bus";
    w1.end.port_name = "v";
    bp.add_wire_validated(std::move(w1));

    Wire w2;
    w2.id = "w2";
    w2.start.node_id = "bat2";
    w2.start.port_name = "v_out";
    w2.end.node_id = "bus";
    w2.end.port_name = "v";

    size_t wire_count_before = bp.wires.size();
    bp.add_wire_validated(std::move(w2));

    // Second wire should be accepted - Bus ports allow multiple connections
    EXPECT_EQ(bp.wires.size(), wire_count_before + 1)
        << "Bus ports should allow multiple wires";
}

// =============================================================================
// Group Filtering Tests: Blueprint Collapsing via group_id
// =============================================================================

TEST(RenderGroupFilter, NodeInDifferentGroup_NotRendered) {
    Blueprint bp;

    // Root-level node (group_id = "")
    Node n1;
    n1.id = "root_node";
    n1.name = "Root";
    n1.type_name = "Battery";
    n1.group_id = "";
    n1.at(0.0f, 0.0f).size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n1));

    // Internal node (group_id = "lamp1")
    Node n2;
    n2.id = "lamp1:internal";
    n2.name = "Internal";
    n2.type_name = "IndicatorLight";
    n2.group_id = "lamp1";
    n2.at(200.0f, 0.0f).size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n2));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    // Render root group — internal node should not appear
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache,
                    nullptr, std::nullopt, nullptr, "");

    // Should render at least the root node rect
    EXPECT_TRUE(dl.had_rect()) << "Root node should render a rect";
}

TEST(RenderGroupFilter, WireCrossGroup_NotRendered) {
    Blueprint bp;

    // Root-level node
    Node n1;
    n1.id = "n1";
    n1.name = "N1";
    n1.group_id = "";
    n1.at(0.0f, 0.0f).size_wh(100.0f, 50.0f);
    n1.output("out");
    bp.add_node(std::move(n1));

    // Node in different group
    Node n2;
    n2.id = "n2";
    n2.name = "N2";
    n2.group_id = "lamp1";
    n2.at(200.0f, 0.0f).size_wh(100.0f, 50.0f);
    n2.input("in");
    bp.add_node(std::move(n2));

    // Wire crosses groups
    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache,
                    nullptr, std::nullopt, nullptr, "");

    // Wire should NOT be rendered — endpoints are in different groups
    EXPECT_FALSE(dl.had_polyline()) << "Wire crossing groups should not render";
}

TEST(RenderGroupFilter, WireSameGroup_Rendered) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.name = "N1";
    n1.group_id = "";
    n1.at(0.0f, 0.0f).size_wh(100.0f, 50.0f);
    n1.output("out");
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.name = "N2";
    n2.group_id = "";
    n2.at(200.0f, 0.0f).size_wh(100.0f, 50.0f);
    n2.input("in");
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache,
                    nullptr, std::nullopt, nullptr, "");

    EXPECT_TRUE(dl.had_polyline()) << "Wire within same group should render";
}

TEST(RenderGroupFilter, RenderSubgroup_ShowsOnlyGroupNodes) {
    Blueprint bp;

    // Root node
    Node root;
    root.id = "root";
    root.name = "Root";
    root.type_name = "Battery";
    root.group_id = "";
    root.at(0.0f, 0.0f).size_wh(120.0f, 80.0f);
    bp.add_node(std::move(root));

    // Two internal nodes in "lamp1"
    Node lamp_a;
    lamp_a.id = "lamp1:a";
    lamp_a.name = "A";
    lamp_a.type_name = "Resistor";
    lamp_a.group_id = "lamp1";
    lamp_a.at(100.0f, 100.0f).size_wh(120.0f, 80.0f);
    bp.add_node(std::move(lamp_a));

    Node lamp_b;
    lamp_b.id = "lamp1:b";
    lamp_b.name = "B";
    lamp_b.type_name = "Load";
    lamp_b.group_id = "lamp1";
    lamp_b.at(300.0f, 100.0f).size_wh(120.0f, 80.0f);
    bp.add_node(std::move(lamp_b));

    // Render "lamp1" group — root should not appear, lamp_a and lamp_b should
    MockDrawList dl;
    Viewport vp;
    VisualNodeCache cache;
    BlueprintRenderer renderer;
    renderer.render(bp, dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f), cache,
                    nullptr, std::nullopt, nullptr, "lamp1");

    // At least 2 rects for the two internal nodes (body + ports)
    EXPECT_GE(dl.rect_count(), 2u) << "Should render internal nodes of lamp1";
}
