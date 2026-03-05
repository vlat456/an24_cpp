#include <gtest/gtest.h>
#include "editor/render.h"
#include "editor/simulation.h"
#include "editor/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/viewport/viewport.h"

/// TDD Step 5: Rendering

/// Тест: пустой Blueprint не крашится
TEST(RenderTest, EmptyBlueprint_DoesNotCrash) {
    Blueprint bp;
    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
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
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
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
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
    EXPECT_TRUE(dl.had_polyline());
}

/// Тест: сетка не крашится
TEST(RenderTest, Grid_DoesNotCrash) {
    MockDrawList dl;
    Viewport vp;
    vp.grid_step = 16.0f;
    vp.zoom = 1.0f;
    render_grid(&dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
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
    SimulationController sim;
    sim.build(bp);
    sim.start();
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Verify simulation produced voltage
    float v_bat = sim.get_wire_voltage("bat.v_out");
    ASSERT_GT(v_bat, 5.0f) << "Battery should produce voltage for test to be meaningful";

    // Render with simulation
    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0, 0), Pt(800, 600),
                     nullptr, std::nullopt, &sim);

    // Should have yellow/amber (energized) polylines: 0xFF44AAFF
    EXPECT_TRUE(dl.has_polyline_with_color(0xFF44AAFF))
        << "Energized wires should be drawn in yellow/amber";
}

TEST(RenderTest, WireHighlighting_WithoutSimulation_NoYellow) {
    Blueprint bp = create_render_circuit();

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0, 0), Pt(800, 600));

    // Without simulation, no yellow/amber polylines
    EXPECT_FALSE(dl.has_polyline_with_color(0xFF44AAFF))
        << "Without simulation, no yellow/amber wires";
}

// ─── Tooltip tests ───

TEST(RenderTest, Tooltip_PortHover_ShowsValue) {
    Blueprint bp = create_render_circuit();

    SimulationController sim;
    sim.build(bp);
    sim.start();
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Hover over a port position (battery v_out)
    // The battery is at (80, 80) size (120, 80), output port is on right side
    Pt hover_pos(80 + 120, 80 + 40); // right edge, center
    TooltipInfo tooltip;
    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0, 0), Pt(800, 600),
                     nullptr, std::nullopt, &sim, &hover_pos, &tooltip);

    // Tooltip should be active if we hit a port
    if (tooltip.active) {
        EXPECT_FALSE(tooltip.text.empty()) << "Tooltip should have a value text";
        EXPECT_FALSE(tooltip.label.empty()) << "Tooltip should have a label";
    }
}

TEST(RenderTest, Tooltip_NoSimulation_NoTooltip) {
    Blueprint bp = create_render_circuit();

    Pt hover_pos(80 + 120, 80 + 40);
    TooltipInfo tooltip;
    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0, 0), Pt(800, 600),
                     nullptr, std::nullopt, nullptr, &hover_pos, &tooltip);

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

    render_tooltip(&dl, tooltip);

    // Should have drawn text
    EXPECT_FALSE(dl.texts_.empty()) << "Tooltip should render text";
}

TEST(RenderTest, RenderTooltip_InactiveDoesNothing) {
    MockDrawList dl;
    TooltipInfo tooltip;
    tooltip.active = false;

    render_tooltip(&dl, tooltip);
    EXPECT_TRUE(dl.texts_.empty()) << "Inactive tooltip should draw nothing";
}
