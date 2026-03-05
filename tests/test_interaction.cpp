#include <gtest/gtest.h>
#include "editor/interaction.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"

/// TDD Step 4: Interaction state

TEST(InteractionTest, DefaultIsEmpty) {
    Interaction i;
    EXPECT_TRUE(i.selected_nodes.empty());
    EXPECT_FALSE(i.selected_wire.has_value());
    EXPECT_EQ(i.dragging, Dragging::None);
    EXPECT_FALSE(i.panning);
}

TEST(InteractionTest, SelectNode) {
    Interaction i;
    i.add_node_selection(5);
    ASSERT_FALSE(i.selected_nodes.empty());
    EXPECT_EQ(i.selected_nodes[0], 5);
}

TEST(InteractionTest, SelectWire) {
    Interaction i;
    i.selected_wire = 3;
    ASSERT_TRUE(i.selected_wire.has_value());
    EXPECT_EQ(*i.selected_wire, 3);
}

TEST(InteractionTest, ClearSelection) {
    Interaction i;
    i.add_node_selection(5);
    i.selected_wire = 3;
    i.clear_selection();
    EXPECT_TRUE(i.selected_nodes.empty());
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
    i.start_drag_routing_point(2, 5, Pt(50.0f, 60.0f)); // wire 2, point 5
    EXPECT_EQ(i.dragging, Dragging::RoutingPoint);
    EXPECT_EQ(i.drag_anchor.x, 50.0f);
    EXPECT_EQ(i.drag_anchor.y, 60.0f);
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

// ============================================================================
// Wire Creation State (TDD for drag-drop wire creation)
// ============================================================================

TEST(WireCreationTest, StartWireCreation_SetsDraggingState) {
    Interaction i;
    i.start_wire_creation("batt1", "v_out", PortSide::Output, Pt(100.0f, 50.0f));

    EXPECT_EQ(i.dragging, Dragging::CreatingWire);
    EXPECT_TRUE(i.has_wire_start());
}

TEST(WireCreationTest, StartWireCreation_TracksPortInfo) {
    Interaction i;
    i.start_wire_creation("batt1", "v_out", PortSide::Output, Pt(100.0f, 50.0f));

    EXPECT_EQ(i.get_wire_start_pos().x, 100.0f);
    EXPECT_EQ(i.get_wire_start_pos().y, 50.0f);
}

TEST(WireCreationTest, ClearWireCreation_ResetsState) {
    Interaction i;
    i.start_wire_creation("batt1", "v_out", PortSide::Output, Pt(100.0f, 50.0f));
    ASSERT_TRUE(i.has_wire_start());

    i.clear_wire_creation();

    EXPECT_FALSE(i.has_wire_start());
    EXPECT_EQ(i.dragging, Dragging::None);
}

TEST(WireCreationTest, MultipleWireCreations_OverridesPrevious) {
    Interaction i;
    i.start_wire_creation("batt1", "v_out", PortSide::Output, Pt(100.0f, 50.0f));
    i.start_wire_creation("load1", "v_in", PortSide::Input, Pt(200.0f, 100.0f));

    EXPECT_EQ(i.get_wire_start_pos().x, 200.0f);
    EXPECT_EQ(i.get_wire_start_pos().y, 100.0f);
}
