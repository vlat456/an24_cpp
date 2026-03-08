#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/visual/scene/scene.h"
#include "editor/input/canvas_input.h"
#include "editor/visual/scene/wire_manager.h"

// =============================================================================
// Phase 1: Context Menu Tests — TDD
// =============================================================================

// Helper: blueprint with one node at (0, 0) size 120x80
static Blueprint make_single_node_bp() {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.type_name = "Battery";
    n.kind = NodeKind::Node;
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");
    bp.add_node(std::move(n));
    return bp;
}

TEST(ContextMenu, RightClickOnNode_SetsNodeContextMenu) {
    Blueprint bp = make_single_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Node is at (0,0) with size (120, 80). Center is (60, 40).
    // With default viewport (zoom=1, pan=0), screen == world.
    auto result = input.on_mouse_down(Pt(60, 40), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_node_context_menu)
        << "Right-click on a node should set show_node_context_menu";
    EXPECT_EQ(result.context_menu_node_index, 0u)
        << "Should identify node index 0";
    EXPECT_FALSE(result.show_context_menu)
        << "Empty-space context menu should NOT be triggered";
}

TEST(ContextMenu, RightClickOnEmpty_StillShowsAddMenu) {
    Blueprint bp = make_single_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Click far from the node (500, 500) — empty space
    auto result = input.on_mouse_down(Pt(500, 500), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_context_menu)
        << "Right-click on empty space should show add-component menu";
    EXPECT_FALSE(result.show_node_context_menu)
        << "Node context menu should NOT be triggered on empty space";
}

TEST(ContextMenu, RightClickOnSecondNode_ReportsCorrectIndex) {
    Blueprint bp;
    Node n1;
    n1.id = "a";
    n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "b";
    n2.type_name = "Resistor";
    n2.at(300, 0).size_wh(120, 80);
    bp.add_node(std::move(n2));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Click on second node center (360, 40)
    auto result = input.on_mouse_down(Pt(360, 40), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_node_context_menu);
    EXPECT_EQ(result.context_menu_node_index, 1u)
        << "Should identify second node (index 1)";
}
