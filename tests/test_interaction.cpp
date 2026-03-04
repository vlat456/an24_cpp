#include <gtest/gtest.h>
#include "editor/interaction.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"

/// TDD Step 4: Interaction state

TEST(InteractionTest, DefaultIsEmpty) {
    Interaction i;
    EXPECT_FALSE(i.selected_node.has_value());
    EXPECT_FALSE(i.selected_wire.has_value());
    EXPECT_EQ(i.dragging, Dragging::None);
    EXPECT_FALSE(i.panning);
}

TEST(InteractionTest, SelectNode) {
    Interaction i;
    i.selected_node = 5;
    ASSERT_TRUE(i.selected_node.has_value());
    EXPECT_EQ(*i.selected_node, 5);
}

TEST(InteractionTest, SelectWire) {
    Interaction i;
    i.selected_wire = 3;
    ASSERT_TRUE(i.selected_wire.has_value());
    EXPECT_EQ(*i.selected_wire, 3);
}

TEST(InteractionTest, ClearSelection) {
    Interaction i;
    i.selected_node = 5;
    i.clear_selection();
    EXPECT_FALSE(i.selected_node.has_value());
    EXPECT_FALSE(i.selected_wire.has_value());
}

TEST(InteractionTest, StartDragNode) {
    Interaction i;
    i.start_drag_node(Pt(100.0f, 200.0f));
    EXPECT_EQ(i.dragging, Dragging::Node);
    EXPECT_EQ(i.drag_anchor.x, 100.0f);
    EXPECT_EQ(i.drag_anchor.y, 200.0f);
}

TEST(InteractionTest, StartDragRoutingPoint) {
    Interaction i;
    i.start_drag_routing_point(2, 5); // wire 2, point 5
    EXPECT_EQ(i.dragging, Dragging::RoutingPoint);
}

TEST(InteractionTest, UpdateDragAnchor) {
    Interaction i;
    i.start_drag_node(Pt(100.0f, 200.0f));
    i.update_drag_anchor(Pt(10.0f, 20.0f)); // добавить смещение
    EXPECT_EQ(i.drag_anchor.x, 110.0f);
    EXPECT_EQ(i.drag_anchor.y, 220.0f);
}

TEST(InteractionTest, EndDrag) {
    Interaction i;
    i.start_drag_node(Pt(0.0f, 0.0f));
    i.end_drag();
    EXPECT_EQ(i.dragging, Dragging::None);
}

TEST(InteractionTest, SetPanning) {
    Interaction i;
    i.set_panning(true);
    EXPECT_TRUE(i.panning);
    i.set_panning(false);
    EXPECT_FALSE(i.panning);
}
