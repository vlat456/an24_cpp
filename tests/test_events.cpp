#include <gtest/gtest.h>
#include "editor/app.h"
#include "editor/data/node.h"
#include "editor/trigonometry.h"

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
    app.add_component("Voltmeter", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const auto& nc = app.blueprint.nodes[0].node_content;
    EXPECT_EQ(nc.type, NodeContentType::Gauge) << "[c3d4] Voltmeter should have Gauge content";
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

    Pt port_pos = visual->getPort(node.outputs[0].name)->worldPosition();

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

    Pt batt_out_pos = batt_visual->getPort(batt.outputs[0].name)->worldPosition();
    Pt load_in_pos = load_visual->getPort(load.inputs[0].name)->worldPosition();

    // Start dragging from battery output
    app.on_mouse_down(batt_out_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Simulate dragging to load input position
    Pt drag_delta = load_in_pos - batt_out_pos;
    app.on_mouse_drag(drag_delta, Pt(0.0f, 0.0f));

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

    Pt batt1_out_pos = batt1_visual->getPort(batt1.outputs[0].name)->worldPosition();
    Pt batt2_out_pos = batt2_visual->getPort(batt2.outputs[0].name)->worldPosition();

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

    Pt port_pos = visual->getPort(node.outputs[0].name)->worldPosition();

    // Start dragging from output port
    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Release on empty space
    app.on_mouse_up(MouseButton::Left);

    // Should cancel wire creation
    EXPECT_EQ(app.interaction.dragging, Dragging::None);
    EXPECT_FALSE(app.interaction.has_wire_start());
}

// ============================================================================
// Wire Reconnection Tests [m6i8j0k2]
// ============================================================================

// Click-drag on a port with an existing wire should enter ReconnectingWire state
TEST(WireReconnectionTest, ClickOnOccupiedPort_StartsReconnection) {
    EditorApp app;

    // Add battery and load, connect them
    app.add_component("Battery", Pt(100, 100));
    app.add_component("HighPowerLoad", Pt(300, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 2);

    const auto& batt = app.blueprint.nodes[0];
    const auto& load = app.blueprint.nodes[1];

    // Create a wire manually between battery output and load input
    Wire w = Wire::make("w1",
        WireEnd(batt.id.c_str(), batt.outputs[0].name.c_str(), PortSide::Output),
        WireEnd(load.id.c_str(), load.inputs[0].name.c_str(), PortSide::Input));
    app.blueprint.add_wire(std::move(w));
    app.visual_cache.onWireAdded(app.blueprint.wires.back(), app.blueprint.nodes);

    // Get the position of the battery output port (occupied)
    auto* batt_visual = app.visual_cache.getOrCreate(batt);
    Pt port_pos = batt_visual->getPort(batt.outputs[0].name)->worldPosition();

    // Click on the occupied port
    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));

    // Should start reconnection, not wire creation
    EXPECT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);
}

// Release reconnecting wire on valid port reconnects the wire
TEST(WireReconnectionTest, ReleaseOnValidPort_ReconnectsWire) {
    EditorApp app;

    // battery → load1, then reconnect to load2
    app.add_component("Battery", Pt(100, 100));
    app.add_component("HighPowerLoad", Pt(300, 100));
    app.add_component("HighPowerLoad", Pt(500, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 3);

    const auto& batt = app.blueprint.nodes[0];
    const auto& load1 = app.blueprint.nodes[1];
    const auto& load2 = app.blueprint.nodes[2];

    Wire w = Wire::make("w1",
        WireEnd(batt.id.c_str(), batt.outputs[0].name.c_str(), PortSide::Output),
        WireEnd(load1.id.c_str(), load1.inputs[0].name.c_str(), PortSide::Input));
    app.blueprint.add_wire(std::move(w));
    app.visual_cache.onWireAdded(app.blueprint.wires.back(), app.blueprint.nodes);

    // Get port positions
    auto* load1_visual = app.visual_cache.getOrCreate(load1);
    Pt load1_port = load1_visual->getPort(load1.inputs[0].name)->worldPosition();

    // Start reconnection from load1's input port (end of wire)
    app.on_mouse_down(load1_port, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);

    // Drag to load2's input port
    auto* load2_visual = app.visual_cache.getOrCreate(load2);
    Pt load2_port = load2_visual->getPort(load2.inputs[0].name)->worldPosition();
    Pt delta = load2_port - load1_port;
    app.on_mouse_drag(delta, Pt(0.0f, 0.0f));

    // Release on load2
    app.on_mouse_up(MouseButton::Left);

    // Wire should still exist, but now connected to load2
    ASSERT_EQ(app.blueprint.wires.size(), 1);
    EXPECT_EQ(app.blueprint.wires[0].end.node_id, load2.id);
    EXPECT_EQ(app.interaction.dragging, Dragging::None);
}

// Release reconnecting wire on empty space deletes the wire
TEST(WireReconnectionTest, ReleaseOnEmpty_DeletesWire) {
    EditorApp app;

    app.add_component("Battery", Pt(100, 100));
    app.add_component("HighPowerLoad", Pt(300, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 2);

    const auto& batt = app.blueprint.nodes[0];
    const auto& load = app.blueprint.nodes[1];

    Wire w = Wire::make("w1",
        WireEnd(batt.id.c_str(), batt.outputs[0].name.c_str(), PortSide::Output),
        WireEnd(load.id.c_str(), load.inputs[0].name.c_str(), PortSide::Input));
    app.blueprint.add_wire(std::move(w));
    app.visual_cache.onWireAdded(app.blueprint.wires.back(), app.blueprint.nodes);

    // Start reconnection
    auto* load_visual = app.visual_cache.getOrCreate(load);
    Pt port_pos = load_visual->getPort(load.inputs[0].name)->worldPosition();
    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));
    ASSERT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);

    // Drag to empty space
    app.on_mouse_drag(Pt(999.0f, 999.0f), Pt(0.0f, 0.0f));
    app.on_mouse_up(MouseButton::Left);

    // Wire should be deleted
    EXPECT_EQ(app.blueprint.wires.size(), 0);
    EXPECT_EQ(app.interaction.dragging, Dragging::None);
}

// Click on a port with NO existing wire should still create a new wire
TEST(WireReconnectionTest, ClickOnFreePort_StartsCreatingWire) {
    EditorApp app;

    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);

    const auto& batt = app.blueprint.nodes[0];

    // Get the position of the output port (no wire attached)
    auto* visual = app.visual_cache.getOrCreate(batt);
    Pt port_pos = visual->getPort(batt.outputs[0].name)->worldPosition();

    app.on_mouse_down(port_pos, MouseButton::Left, Pt(0.0f, 0.0f));

    // Should start wire creation (not reconnection)
    EXPECT_EQ(app.interaction.dragging, Dragging::CreatingWire);
}

// ============================================================================
// Routing Point Insertion Fix [j3f5a7b9]
// ============================================================================

// Routing point should be inserted into the correct segment, not seeded with
// phantom direct-line distance that could pick the wrong segment.
TEST(RoutingPointInsertionTest, InsertsIntoCorrectSegment) {
    EditorApp app;
    app.blueprint.grid_step = 8.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(400, 0); n2.size = Pt(120, 80); n2.input("i");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    // L-shaped routing: down then right
    w.add_routing_point(Pt(120.0f, 200.0f));
    w.add_routing_point(Pt(400.0f, 200.0f));
    app.blueprint.add_wire(std::move(w));

    // Double-click near the horizontal segment (y≈200)
    app.on_double_click(Pt(260.0f, 201.0f));

    // Should now have 3 routing points (one inserted)
    EXPECT_EQ(app.blueprint.wires[0].routing_points.size(), 3);
}

// ============================================================================
// [g1h2i3j4] Regression: Bus disconnect selects correct wire by alias port
// ============================================================================

TEST(BusDisconnectTest, DisconnectsCorrectWireByAlias) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    // Bus node
    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    app.blueprint.add_node(std::move(bus));

    // Two source nodes
    Node n1; n1.id = "a"; n1.name = "A"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.name = "B"; n2.pos = Pt(0, 200); n2.size = Pt(120, 80);
    n2.output("o");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    // Two wires to the bus
    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v"));
    app.blueprint.add_wire(std::move(w1));
    app.blueprint.add_wire(std::move(w2));

    // Ensure cache is built
    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Click on w2's alias port on the bus
    Pt w2_pos = editor_math::get_port_position(
        app.blueprint.nodes[0], "v", app.blueprint.wires, "w2", &app.visual_cache);
    app.on_mouse_down(w2_pos, MouseButton::Left, Pt(0, 0));

    // Should start reconnecting wire w2 (index 1), NOT w1 (index 0)
    EXPECT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);
    EXPECT_EQ(app.interaction.get_reconnect_wire_index(), 1u);
}

// ============================================================================
// [h2i3j4k5] Regression: reconnect anchor uses nearest routing point
// ============================================================================

TEST(ReconnectAnchorTest, AnchorIsLastRoutingPoint) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(400, 0); n2.size = Pt(120, 80);
    n2.input("i");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(200, 100));
    w.add_routing_point(Pt(300, 100));
    app.blueprint.add_wire(std::move(w));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Click on end port of node "b" (input "i")
    auto* bv = app.visual_cache.get("b");
    Pt end_port_pos = bv->getPort("i")->worldPosition();
    app.on_mouse_down(end_port_pos, MouseButton::Left, Pt(0, 0));

    EXPECT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);
    // Anchor should be the LAST routing point (300,100), not the start port
    Pt anchor = app.interaction.get_reconnect_anchor_pos();
    EXPECT_FLOAT_EQ(anchor.x, 300.0f);
    EXPECT_FLOAT_EQ(anchor.y, 100.0f);
}

TEST(ReconnectAnchorTest, AnchorIsFirstRoutingPoint_WhenDetachStart) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(400, 0); n2.size = Pt(120, 80);
    n2.input("i");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(200, 100));
    w.add_routing_point(Pt(300, 100));
    app.blueprint.add_wire(std::move(w));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Click on start port of node "a" (output "o")
    auto* av = app.visual_cache.get("a");
    Pt start_port_pos = av->getPort("o")->worldPosition();
    app.on_mouse_down(start_port_pos, MouseButton::Left, Pt(0, 0));

    EXPECT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);
    // Anchor should be the FIRST routing point (200,100), not the end port
    Pt anchor = app.interaction.get_reconnect_anchor_pos();
    EXPECT_FLOAT_EQ(anchor.x, 200.0f);
    EXPECT_FLOAT_EQ(anchor.y, 100.0f);
}

// ============================================================================
// [a1b2c3d4] Regression: disconnect then reconnect to same port keeps wire
// ============================================================================

TEST(ReconnectTest, DropOnSamePort_KeepsWireUnchanged) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(400, 0); n2.size = Pt(120, 80);
    n2.input("i");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(200, 50));
    w.add_routing_point(Pt(300, 50));
    app.blueprint.add_wire(std::move(w));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Click on end port of node "b" (input "i") — starts reconnection
    auto* bv = app.visual_cache.get("b");
    Pt end_port_pos = bv->getPort("i")->worldPosition();
    app.on_mouse_down(end_port_pos, MouseButton::Left, Pt(0, 0));
    ASSERT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);

    // Release on the SAME port — wire should be unchanged
    app.last_mouse_pos = end_port_pos;
    app.on_mouse_up(MouseButton::Left);

    // Wire should still exist with original routing points
    ASSERT_EQ(app.blueprint.wires.size(), 1u);
    EXPECT_EQ(app.blueprint.wires[0].id, "w1");
    EXPECT_EQ(app.blueprint.wires[0].end.node_id, "b");
    EXPECT_EQ(app.blueprint.wires[0].end.port_name, "i");
    EXPECT_EQ(app.blueprint.wires[0].routing_points.size(), 2u);
    EXPECT_FLOAT_EQ(app.blueprint.wires[0].routing_points[0].x, 200.0f);
    EXPECT_FLOAT_EQ(app.blueprint.wires[0].routing_points[1].x, 300.0f);
}

TEST(ReconnectTest, DropOnSamePort_BusAlias_KeepsWireUnchanged) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    app.blueprint.add_node(std::move(bus));

    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    app.blueprint.add_node(std::move(n1));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    app.blueprint.add_wire(std::move(w));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Click on Bus alias port for wire "w1"
    Pt w1_pos = editor_math::get_port_position(
        app.blueprint.nodes[0], "v", app.blueprint.wires, "w1", &app.visual_cache);
    app.on_mouse_down(w1_pos, MouseButton::Left, Pt(0, 0));
    ASSERT_EQ(app.interaction.dragging, Dragging::ReconnectingWire);

    // Drop back on the same alias port
    app.last_mouse_pos = w1_pos;
    app.on_mouse_up(MouseButton::Left);

    // Wire should still exist unchanged
    ASSERT_EQ(app.blueprint.wires.size(), 1u);
    EXPECT_EQ(app.blueprint.wires[0].id, "w1");
    EXPECT_EQ(app.blueprint.wires[0].end.node_id, "bus1");
}

// ============================================================================
// [k2l3m4n5] Regression: clicking unassigned Bus "v" port starts wire creation
// ============================================================================

TEST(BusNewPortTest, ClickUnassignedV_StartsWireCreation_NotReconnection) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    app.blueprint.add_node(std::move(bus));

    // Two wires already connected to bus
    Node n1; n1.id = "a"; n1.pos = Pt(0, 0); n1.size = Pt(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.pos = Pt(0, 200); n2.size = Pt(120, 80);
    n2.output("o");
    app.blueprint.add_node(std::move(n1));
    app.blueprint.add_node(std::move(n2));

    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v"));
    app.blueprint.add_wire(std::move(w1));
    app.blueprint.add_wire(std::move(w2));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Get position of the unassigned "v" port (last port on Bus)
    auto* bus_visual = app.visual_cache.get("bus1");
    ASSERT_NE(bus_visual, nullptr);
    Pt v_pos = bus_visual->getPort("v")->worldPosition();  // main "v" (no wire_id)

    app.on_mouse_down(v_pos, MouseButton::Left, Pt(0, 0));

    // Should start wire CREATION, not reconnection
    EXPECT_EQ(app.interaction.dragging, Dragging::CreatingWire);
}

// ============================================================================
// [k2l3m4n5] Regression: RefNode can connect to Bus (both directions)
// ============================================================================

TEST(RefBusConnectionTest, RefNode_ConnectsTo_Bus) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node ref; ref.id = "ref1"; ref.name = "GND"; ref.kind = NodeKind::Ref;
    ref.at(0, 0).size_wh(40, 40);
    ref.output("v");
    app.blueprint.add_node(std::move(ref));

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    app.blueprint.add_node(std::move(bus));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Start wire from RefNode port
    auto* ref_visual = app.visual_cache.get("ref1");
    Pt ref_port = ref_visual->getPort("v")->worldPosition();
    app.on_mouse_down(ref_port, MouseButton::Left, Pt(0, 0));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire);

    // Drop on Bus "v" port
    auto* bus_visual = app.visual_cache.get("bus1");
    Pt bus_port = bus_visual->getPort("v")->worldPosition();
    app.last_mouse_pos = bus_port;
    app.on_mouse_up(MouseButton::Left);

    // Wire should be created
    EXPECT_EQ(app.blueprint.wires.size(), 1u);
}

TEST(RefBusConnectionTest, Bus_ConnectsTo_RefNode) {
    EditorApp app;
    app.blueprint.grid_step = 16.0f;

    Node ref; ref.id = "ref1"; ref.name = "GND"; ref.kind = NodeKind::Ref;
    ref.at(0, 0).size_wh(40, 40);
    ref.output("v");
    app.blueprint.add_node(std::move(ref));

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    app.blueprint.add_node(std::move(bus));

    // Add an existing wire to bus so bug (b) would have triggered
    Node n1; n1.id = "a"; n1.pos = Pt(0, 200); n1.size = Pt(120, 80);
    n1.output("o");
    app.blueprint.add_node(std::move(n1));
    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    app.blueprint.add_wire(std::move(w1));

    for (auto& n : app.blueprint.nodes)
        app.visual_cache.getOrCreate(n, app.blueprint.wires);

    // Start wire from Bus unassigned "v" port
    auto* bus_visual = app.visual_cache.get("bus1");
    Pt bus_port = bus_visual->getPort("v")->worldPosition();
    app.on_mouse_down(bus_port, MouseButton::Left, Pt(0, 0));
    ASSERT_EQ(app.interaction.dragging, Dragging::CreatingWire)
        << "Should start wire creation from Bus 'v' port, not reconnection";

    // Drop on RefNode port
    auto* ref_visual = app.visual_cache.get("ref1");
    Pt ref_port = ref_visual->getPort("v")->worldPosition();
    app.last_mouse_pos = ref_port;
    app.on_mouse_up(MouseButton::Left);

    // Wire should be created (total 2: existing w1 + new one)
    EXPECT_EQ(app.blueprint.wires.size(), 2u);
}
