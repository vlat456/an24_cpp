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

    app.on_mouse_down(Pt(0.0f, 0.0f), MouseButton::Middle, Pt(0.0f, 0.0f));
    app.on_mouse_drag(Pt(100.0f, 50.0f), Pt(0.0f, 0.0f));
    app.on_mouse_up(MouseButton::Middle);

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
