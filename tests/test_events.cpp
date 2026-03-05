#include <gtest/gtest.h>
#include "editor/app.h"
#include "editor/data/node.h"

/// TDD Step 7: Event handling

TEST(EventsTest, DefaultApp_HasEmptyBlueprint) {
    EditorApp app;
    EXPECT_TRUE(app.blueprint.nodes.empty());
    EXPECT_TRUE(app.blueprint.wires.empty());
}

TEST(EventsTest, NewCircuit_CreatesEmpty) {
    EditorApp app;

    // Добавляем узел
    Node n;
    n.id = "test";
    n.at(100.0f, 50.0f);
    app.blueprint.add_node(std::move(n));

    // new_circuit() очищает
    app.new_circuit();
    EXPECT_TRUE(app.blueprint.nodes.empty());
}

TEST(EventsTest, MouseDown_OnNode_Selects) {
    EditorApp app;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    app.blueprint.add_node(std::move(n));

    // Клик на узел (внутри rect)
    app.on_mouse_down(Pt(150.0f, 90.0f), MouseButton::Left, Pt(0.0f, 0.0f));

    ASSERT_FALSE(app.interaction.selected_nodes.empty());
    EXPECT_EQ(app.interaction.selected_nodes[0], 0);
}

TEST(EventsTest, MouseDown_Empty_SelectsNothing) {
    EditorApp app;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    app.blueprint.add_node(std::move(n));

    // Клик вне узла
    app.on_mouse_down(Pt(10.0f, 10.0f), MouseButton::Left, Pt(0.0f, 0.0f));

    EXPECT_TRUE(app.interaction.selected_nodes.empty());
}

TEST(EventsTest, MouseDrag_UpdatesViewport) {
    EditorApp app;
    float old_pan_x = app.viewport.pan.x;

    // Click on empty space starts panning (Left button in empty area)
    app.on_mouse_down(Pt(500.0f, 500.0f), MouseButton::Left, Pt(0.0f, 0.0f));
    app.on_mouse_drag(Pt(100.0f, 50.0f), Pt(0.0f, 0.0f));
    app.on_mouse_up(MouseButton::Left);

    // Pan должен измениться
    EXPECT_NE(app.viewport.pan.x, old_pan_x);
}

TEST(EventsTest, MouseWheel_Zooms) {
    EditorApp app;
    float old_zoom = app.viewport.zoom;

    app.on_scroll(0.1f, Pt(400.0f, 300.0f), Pt(0.0f, 0.0f));

    EXPECT_GT(app.viewport.zoom, old_zoom);
}

TEST(EventsTest, KeyDown_Escape_ClearsSelection) {
    EditorApp app;
    app.interaction.add_node_selection(5);

    app.on_key_down(Key::Escape);

    EXPECT_TRUE(app.interaction.selected_nodes.empty());
}

// ============================================================================
// Snap-while-dragging tests
// ============================================================================

TEST(EventsTest, DragNode_SnapsToGridDuringDrag) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n;
    n.id = "n1";
    n.pos = Pt(32.0f, 64.0f); // already on grid
    n.size = Pt(120.0f, 80.0f);
    app.blueprint.add_node(std::move(n));

    // Click on node to start dragging
    app.on_mouse_down(Pt(50.0f, 70.0f), MouseButton::Left, Pt(0.0f, 0.0f));
    EXPECT_EQ(app.interaction.dragging, Dragging::Node);

    // Drag by 5px (less than half grid step) — should NOT change position
    app.on_mouse_drag(Pt(5.0f, 3.0f), Pt(0.0f, 0.0f));
    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.x, 32.0f);
    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.y, 64.0f);

    // Drag by another 12px (total 17px > 16/2) — snaps to next grid cell
    app.on_mouse_drag(Pt(12.0f, 0.0f), Pt(0.0f, 0.0f));
    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.x, 48.0f); // 32+17=49, round(49/16)*16=48
    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.y, 64.0f);

    app.on_mouse_up(MouseButton::Left);
    // Position stays snapped after release
    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.x, 48.0f);
}

TEST(EventsTest, DragNode_SmallDeltasAccumulate) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n;
    n.id = "n1";
    n.pos = Pt(0.0f, 0.0f);
    n.size = Pt(120.0f, 80.0f);
    app.blueprint.add_node(std::move(n));

    app.on_mouse_down(Pt(50.0f, 50.0f), MouseButton::Left, Pt(0.0f, 0.0f));

    // 16 tiny steps of 1px each = 16px total → should snap to grid=16
    for (int i = 0; i < 16; i++)
        app.on_mouse_drag(Pt(1.0f, 0.0f), Pt(0.0f, 0.0f));

    EXPECT_FLOAT_EQ(app.blueprint.nodes[0].pos.x, 16.0f);
    app.on_mouse_up(MouseButton::Left);
}

TEST(EventsTest, DragRoutingPoint_SnapsToGridDuringDrag) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(300, 0); n2.size = Pt(120, 80); n2.input("i");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(160.0f, 32.0f)); // on grid
    app.blueprint.add_wire(std::move(w));

    // Click on the routing point
    app.on_mouse_down(Pt(160.0f, 32.0f), MouseButton::Left, Pt(0.0f, 0.0f));
    EXPECT_EQ(app.interaction.dragging, Dragging::RoutingPoint);

    // Drag by 20px (>16/2) → snaps to 176
    app.on_mouse_drag(Pt(20.0f, 0.0f), Pt(0.0f, 0.0f));
    EXPECT_FLOAT_EQ(app.blueprint.wires[0].routing_points[0].x, 176.0f);

    app.on_mouse_up(MouseButton::Left);
    EXPECT_FLOAT_EQ(app.blueprint.wires[0].routing_points[0].x, 176.0f);
}

TEST(EventsTest, DragMultipleNodes_PreservesRelativePositions) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    Node n2; n2.id = "b"; n2.pos = Pt(160, 0); n2.size = Pt(120, 80);
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    // Select first, add second
    app.on_mouse_down(Pt(50, 40), MouseButton::Left, Pt(0, 0));
    app.on_mouse_up(MouseButton::Left);
    app.on_mouse_down(Pt(200, 40), MouseButton::Left, Pt(0, 0), true);

    // Now drag
    app.on_mouse_drag(Pt(32.0f, 0.0f), Pt(0.0f, 0.0f));

    float dx = app.blueprint.nodes[1].pos.x - app.blueprint.nodes[0].pos.x;
    EXPECT_FLOAT_EQ(dx, 160.0f); // relative offset preserved

    app.on_mouse_up(MouseButton::Left);
}

// ─── Regression tests for add_component ───

// [a1b2] Bus/Ref nodes must get size 40x40, not 120x80
TEST(EventsTest, AddComponent_BusSize_a1b2) {
    EditorApp app;
    app.add_component("Bus", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& n = app.blueprint.nodes[0];
    EXPECT_EQ(n.kind, NodeKind::Bus);
    EXPECT_FLOAT_EQ(n.size.x, 40.0f) << "[a1b2] Bus size.x should be 40";
    EXPECT_FLOAT_EQ(n.size.y, 40.0f) << "[a1b2] Bus size.y should be 40";
}

TEST(EventsTest, AddComponent_RefNodeSize_a1b2) {
    EditorApp app;
    app.add_component("RefNode", Pt(200, 200));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& n = app.blueprint.nodes[0];
    EXPECT_EQ(n.kind, NodeKind::Ref);
    EXPECT_FLOAT_EQ(n.size.x, 40.0f) << "[a1b2] RefNode size.x should be 40";
    EXPECT_FLOAT_EQ(n.size.y, 40.0f) << "[a1b2] RefNode size.y should be 40";
}

TEST(EventsTest, AddComponent_StandardNodeSize_a1b2) {
    EditorApp app;
    app.add_component("Battery", Pt(300, 300));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& n = app.blueprint.nodes[0];
    EXPECT_EQ(n.kind, NodeKind::Node);
    EXPECT_FLOAT_EQ(n.size.x, 120.0f);
    EXPECT_FLOAT_EQ(n.size.y, 80.0f);
}

// [c3d4] Newly created components should have node_content
TEST(EventsTest, AddComponent_BatteryHasGaugeContent_c3d4) {
    EditorApp app;
    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& nc = app.blueprint.nodes[0].node_content;
    EXPECT_EQ(nc.type, NodeContentType::Gauge) << "[c3d4] Battery should have Gauge content";
    EXPECT_EQ(nc.unit, "V");
}

TEST(EventsTest, AddComponent_IndicatorLightHasTextContent_c3d4) {
    EditorApp app;
    app.add_component("IndicatorLight", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& nc = app.blueprint.nodes[0].node_content;
    EXPECT_EQ(nc.type, NodeContentType::Text) << "[c3d4] IndicatorLight should have Text content";
}

// add_component: ports loaded from ComponentRegistry
TEST(EventsTest, AddComponent_PortsLoadedFromRegistry) {
    EditorApp app;
    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& n = app.blueprint.nodes[0];
    EXPECT_EQ(n.inputs.size(), 1);   // v_in
    EXPECT_EQ(n.outputs.size(), 1);  // v_out
    EXPECT_EQ(n.type_name, "Battery");
}

// add_component: unique ID generation
TEST(EventsTest, AddComponent_UniqueIds) {
    EditorApp app;
    app.add_component("Battery", Pt(100, 100));
    app.add_component("Battery", Pt(200, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 2);
    EXPECT_NE(app.blueprint.nodes[0].id, app.blueprint.nodes[1].id);
}

// ============================================================================
// Wire Creation via Mouse Drag (TDD)
// ============================================================================

TEST(WireCreationTest, MouseDownOnPort_StartsCreatingWire) {
    EditorApp app;

    // Add battery component
    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);

    const auto& node = app.blueprint.nodes[0];
    ASSERT_FALSE(node.outputs.empty());

    // Get the position of the output port
    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);
    ASSERT_NE(visual, nullptr);

    Pt port_pos = visual->getPortPosition(node.outputs[0].name);

    // Mouse down on the output port
    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));

    // Should start wire creation
    EXPECT_EQ(app.interaction.dragging, Dragging::CreatingWire);
    EXPECT_TRUE(app.interaction.has_wire_start());
}

TEST(WireCreationTest, MouseUpOnCompatiblePort_CreatesWire) {
    EditorApp app;

    // Add battery and load components
    app.add_component("Battery", Pt(100, 100));
    app.add_component("HighPowerLoad", Pt(300, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 2);

    const auto& batt = app.blueprint.nodes[0];
    const auto& load = app.blueprint.nodes[1];

    VisualNodeCache cache;
    auto* batt_visual = cache.getOrCreate(batt);
    auto* load_visual = cache.getOrCreate(load);
    ASSERT_NE(batt_visual, nullptr);
    ASSERT_NE(load_visual, nullptr);

    Pt batt_out_pos = batt_visual->getPortPosition(batt.outputs[0].name);
    Pt load_in_pos = load_visual->getPortPosition(load.inputs[0].name);

    // Start dragging from battery output
    app.on_mouse_down(batt_out_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Release on load input
    size_t wire_count_before = app.blueprint.wires.size();
    app.on_mouse_up(MouseButton::Left);
    size_t wire_count_after = app.blueprint.wires.size();

    // Should create a wire
    EXPECT_GT(wire_count_after, wire_count_before);
}

TEST(WireCreationTest, MouseUpOnIncompatiblePort_DoesNotCreateWire) {
    EditorApp app;

    // Add two batteries (output-to-output is invalid)
    app.add_component("Battery", Pt(100, 100));
    app.add_component("Battery", Pt(300, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 2);

    const auto& batt1 = app.blueprint.nodes[0];
    const auto& batt2 = app.blueprint.nodes[1];

    VisualNodeCache cache;
    auto* batt1_visual = cache.getOrCreate(batt1);
    auto* batt2_visual = cache.getOrCreate(batt2);
    ASSERT_NE(batt1_visual, nullptr);
    ASSERT_NE(batt2_visual, nullptr);

    Pt batt1_out_pos = batt1_visual->getPortPosition(batt1.outputs[0].name);
    Pt batt2_out_pos = batt2_visual->getPortPosition(batt2.outputs[0].name);

    // Start dragging from first battery output
    app.on_mouse_down(batt1_out_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Release on second battery output (output-to-output should fail)
    size_t wire_count_before = app.blueprint.wires.size();
    app.on_mouse_up(MouseButton::Left);
    size_t wire_count_after = app.blueprint.wires.size();

    // Should NOT create a wire
    EXPECT_EQ(wire_count_after, wire_count_before);
}

TEST(WireCreationTest, MouseUpOnEmptySpace_CancelsWireCreation) {
    EditorApp app;

    // Add battery component
    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);

    const auto& node = app.blueprint.nodes[0];
    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(node);
    ASSERT_NE(visual, nullptr);

    Pt port_pos = visual->getPortPosition(node.outputs[0].name);

    // Start dragging from output port
    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Release on empty space
    app.on_mouse_up(MouseButton::Left);

    // Should cancel wire creation
    EXPECT_EQ(app.interaction.dragging, Dragging::None);
    EXPECT_FALSE(app.interaction.has_wire_start());
}
