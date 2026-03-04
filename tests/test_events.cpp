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
