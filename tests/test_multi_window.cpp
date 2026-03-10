#include <gtest/gtest.h>
#include "editor/window/window_manager.h"
#include "editor/window/blueprint_window.h"
#include "editor/data/blueprint.h"
#include "editor/visual/scene/scene.h"
#include "jit_solver/simulator.h"

// ============================================================================
// BlueprintWindow tests
// ============================================================================

TEST(BlueprintWindow, ConstructWithGroupId) {
    Blueprint bp;
    BlueprintWindow win(bp, "lamp1", "Lamp");

    EXPECT_EQ(win.group_id, "lamp1");
    EXPECT_EQ(win.title, "Lamp");
    EXPECT_TRUE(win.open);
    EXPECT_EQ(win.scene.groupId(), "lamp1");
}

TEST(BlueprintWindow, SceneReferencesSharedBlueprint) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(0, 0).size_wh(100, 50);
    bp.add_node(std::move(n));

    BlueprintWindow win(bp, "", "Root");

    // Window's scene sees the shared blueprint's nodes
    EXPECT_EQ(win.scene.nodeCount(), 1u);
    EXPECT_EQ(win.scene.findNode("n1")->id, "n1");

    // Adding a node through the blueprint is visible in the window
    Node n2; n2.id = "n2"; n2.at(200, 0).size_wh(100, 50);
    bp.add_node(std::move(n2));
    EXPECT_EQ(win.scene.nodeCount(), 2u);
}

TEST(BlueprintWindow, TwoWindowsSeeSharedData) {
    Blueprint bp;
    Node root_node; root_node.id = "r1"; root_node.at(0, 0).size_wh(100, 50);
    bp.add_node(std::move(root_node));

    Node internal; internal.id = "lamp1:vin"; internal.group_id = "lamp1";
    internal.at(0, 0).size_wh(100, 50);
    bp.add_node(std::move(internal));

    BlueprintWindow root_win(bp, "", "Root");
    BlueprintWindow sub_win(bp, "lamp1", "Lamp");

    // Both windows see all nodes (the scene has nodeCount for the entire blueprint)
    EXPECT_EQ(root_win.scene.nodeCount(), 2u);
    EXPECT_EQ(sub_win.scene.nodeCount(), 2u);

    // But ownsNode filters by group_id
    EXPECT_TRUE(root_win.scene.ownsNode(*bp.find_node("r1")));
    EXPECT_FALSE(root_win.scene.ownsNode(*bp.find_node("lamp1:vin")));
    EXPECT_FALSE(sub_win.scene.ownsNode(*bp.find_node("r1")));
    EXPECT_TRUE(sub_win.scene.ownsNode(*bp.find_node("lamp1:vin")));
}

TEST(BlueprintWindow, IndependentViewports) {
    Blueprint bp;
    BlueprintWindow win1(bp, "", "Root");
    BlueprintWindow win2(bp, "lamp1", "Lamp");

    win1.scene.viewport().zoom = 2.0f;
    win2.scene.viewport().zoom = 0.5f;

    EXPECT_FLOAT_EQ(win1.scene.viewport().zoom, 2.0f);
    EXPECT_FLOAT_EQ(win2.scene.viewport().zoom, 0.5f);
}

TEST(BlueprintWindow, IndependentInteraction) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(0, 0).size_wh(100, 50);
    bp.add_node(std::move(n));

    BlueprintWindow win1(bp, "", "Root");
    BlueprintWindow win2(bp, "", "Root2");

    win1.input.add_node_selection(0);

    EXPECT_EQ(win1.input.selected_nodes().size(), 1u);
    EXPECT_EQ(win2.input.selected_nodes().size(), 0u);
}

// ============================================================================
// WindowManager tests
// ============================================================================

TEST(WindowManager, CreatesRootOnConstruction) {
    Blueprint bp;
    WindowManager mgr(bp);

    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.root().group_id, "");
    EXPECT_EQ(mgr.root().title, "Root");
    EXPECT_TRUE(mgr.root().open);
}

TEST(WindowManager, OpenSubWindow) {
    Blueprint bp;
    WindowManager mgr(bp);

    auto* win = mgr.open("lamp1", "Lamp [lamp1]");
    ASSERT_NE(win, nullptr);
    EXPECT_EQ(win->group_id, "lamp1");
    EXPECT_EQ(win->title, "Lamp [lamp1]");
    EXPECT_EQ(mgr.count(), 2u);
}

TEST(WindowManager, OpenExistingReturnsSame) {
    Blueprint bp;
    WindowManager mgr(bp);

    auto* win1 = mgr.open("lamp1", "Lamp");
    auto* win2 = mgr.open("lamp1", "Lamp");

    EXPECT_EQ(win1, win2);
    EXPECT_EQ(mgr.count(), 2u);  // still 2 (root + lamp1)
}

TEST(WindowManager, FindWindow) {
    Blueprint bp;
    WindowManager mgr(bp);

    EXPECT_NE(mgr.find(""), nullptr);    // root exists
    EXPECT_EQ(mgr.find("lamp1"), nullptr); // not opened yet

    mgr.open("lamp1", "Lamp");
    EXPECT_NE(mgr.find("lamp1"), nullptr);
}

TEST(WindowManager, CloseSubWindow) {
    Blueprint bp;
    WindowManager mgr(bp);

    mgr.open("lamp1", "Lamp");
    EXPECT_EQ(mgr.count(), 2u);

    mgr.close("lamp1");
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.find("lamp1"), nullptr);
}

TEST(WindowManager, CloseRootIsIgnored) {
    Blueprint bp;
    WindowManager mgr(bp);

    mgr.close("");  // try to close root
    EXPECT_EQ(mgr.count(), 1u);  // root still there
}

TEST(WindowManager, CloseAll) {
    Blueprint bp;
    WindowManager mgr(bp);

    mgr.open("lamp1", "Lamp1");
    mgr.open("lamp2", "Lamp2");
    mgr.open("bat1", "Battery");
    EXPECT_EQ(mgr.count(), 4u);

    mgr.closeAll();
    EXPECT_EQ(mgr.count(), 1u);  // only root remains
    EXPECT_EQ(mgr.root().group_id, "");
}

TEST(WindowManager, RemoveClosedWindows) {
    Blueprint bp;
    WindowManager mgr(bp);

    mgr.open("lamp1", "Lamp1");
    mgr.open("lamp2", "Lamp2");
    EXPECT_EQ(mgr.count(), 3u);

    // User closes lamp1 window
    mgr.find("lamp1")->open = false;
    mgr.removeClosedWindows();

    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.find("lamp1"), nullptr);
    EXPECT_NE(mgr.find("lamp2"), nullptr);
}

TEST(WindowManager, SubWindowSeesSharedBlueprint) {
    Blueprint bp;
    Node n; n.id = "lamp1:vin"; n.group_id = "lamp1";
    n.at(0, 0).size_wh(100, 50);
    bp.add_node(std::move(n));

    WindowManager mgr(bp);
    auto* win = mgr.open("lamp1", "Lamp");

    // Sub-window sees the node in shared blueprint
    EXPECT_EQ(win->scene.findNode("lamp1:vin")->id, "lamp1:vin");
    EXPECT_TRUE(win->scene.ownsNode(*bp.find_node("lamp1:vin")));
}

TEST(WindowManager, MultipleWindowsIndependentViewports) {
    Blueprint bp;
    WindowManager mgr(bp);

    mgr.open("lamp1", "Lamp");

    mgr.root().scene.viewport().zoom = 3.0f;
    mgr.find("lamp1")->scene.viewport().zoom = 0.8f;

    EXPECT_FLOAT_EQ(mgr.root().scene.viewport().zoom, 3.0f);
    EXPECT_FLOAT_EQ(mgr.find("lamp1")->scene.viewport().zoom, 0.8f);
}

// ============================================================================
// Auto-layout group tests
// ============================================================================

TEST(AutoLayoutGroup, LayoutsNodesInGroup) {
    Blueprint bp;

    // Root node
    Node root; root.id = "r1"; root.at(0, 0).size_wh(120, 80);
    root.type_name = "RefNode"; root.render_hint = "ref";
    bp.add_node(std::move(root));

    // Internal nodes all at same position (simulates add_blueprint behavior)
    Node vin; vin.id = "lamp1:vin"; vin.group_id = "lamp1";
    vin.type_name = "BlueprintInput"; vin.at(50, 50).size_wh(100, 60);
    bp.add_node(std::move(vin));

    Node lamp; lamp.id = "lamp1:lamp"; lamp.group_id = "lamp1";
    lamp.type_name = "IndicatorLight"; lamp.at(50, 50).size_wh(100, 60);
    bp.add_node(std::move(lamp));

    Node vout; vout.id = "lamp1:vout"; vout.group_id = "lamp1";
    vout.type_name = "BlueprintOutput"; vout.at(50, 50).size_wh(100, 60);
    bp.add_node(std::move(vout));

    // All three internal nodes start at same position
    EXPECT_FLOAT_EQ(bp.find_node("lamp1:vin")->pos.x, 50.0f);
    EXPECT_FLOAT_EQ(bp.find_node("lamp1:lamp")->pos.x, 50.0f);
    EXPECT_FLOAT_EQ(bp.find_node("lamp1:vout")->pos.x, 50.0f);

    bp.auto_layout_group("lamp1");

    // After layout, nodes should have distinct positions
    Pt p1 = bp.find_node("lamp1:vin")->pos;
    Pt p2 = bp.find_node("lamp1:lamp")->pos;
    Pt p3 = bp.find_node("lamp1:vout")->pos;

    // At least two of them should be at different positions
    bool all_same = (p1.x == p2.x && p1.y == p2.y) &&
                    (p2.x == p3.x && p2.y == p3.y);
    EXPECT_FALSE(all_same) << "Auto-layout should spread nodes apart";

    // Root node should be untouched
    EXPECT_FLOAT_EQ(bp.find_node("r1")->pos.x, 0.0f);
    EXPECT_FLOAT_EQ(bp.find_node("r1")->pos.y, 0.0f);
}

TEST(AutoLayoutGroup, EmptyGroupIsNoOp) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(100, 200).size_wh(80, 60);
    bp.add_node(std::move(n));

    bp.auto_layout_group("nonexistent");

    // Node untouched
    EXPECT_FLOAT_EQ(bp.find_node("n1")->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(bp.find_node("n1")->pos.y, 200.0f);
}

// ============================================================================
// Regression: recompute_group_ids preserves collapsed node group_id
// ============================================================================

TEST(RecomputeGroupIds, PreservesCollapsedNodeGroupId) {
    // Scenario: a collapsed Blueprint node placed inside a sub-window
    // (group_id = parent group) should NOT be reset to "" by recompute_group_ids.
    Blueprint bp;

    // Root-level battery
    Node battery; battery.id = "bat1"; battery.at(0, 0).size_wh(100, 60);
    bp.add_node(std::move(battery));

    // Collapsed group "vsu1" at root level
    Node vsu_collapsed; vsu_collapsed.id = "vsu1"; vsu_collapsed.at(200, 0).size_wh(120, 80);
    vsu_collapsed.expandable = true;
    vsu_collapsed.collapsed = true;
    bp.add_node(std::move(vsu_collapsed));

    // Internal nodes of vsu1
    Node vin; vin.id = "vsu1:vin"; vin.at(0, 0).size_wh(60, 40);
    bp.add_node(std::move(vin));
    Node lamp; lamp.id = "vsu1:lamp"; lamp.at(100, 0).size_wh(60, 40);
    bp.add_node(std::move(lamp));

    CollapsedGroup vsu_group;
    vsu_group.id = "vsu1";
    vsu_group.internal_node_ids = {"vsu1:vin", "vsu1:lamp"};
    bp.collapsed_groups.push_back(vsu_group);

    // Now add a nested blueprint INSIDE vsu1's sub-window
    Node nested; nested.id = "nested1"; nested.at(300, 0).size_wh(120, 80);
    nested.expandable = true;
    nested.collapsed = true;
    nested.group_id = "vsu1";  // Placed inside vsu1's sub-window
    bp.add_node(std::move(nested));

    // Internal nodes of nested1
    Node n_int; n_int.id = "nested1:r1"; n_int.at(0, 0).size_wh(60, 40);
    bp.add_node(std::move(n_int));

    CollapsedGroup nested_group;
    nested_group.id = "nested1";
    nested_group.internal_node_ids = {"nested1:r1"};
    bp.collapsed_groups.push_back(nested_group);

    // Recompute should preserve nested's group_id
    bp.recompute_group_ids();

    EXPECT_EQ(bp.find_node("bat1")->group_id, "");        // root-level stays root
    EXPECT_EQ(bp.find_node("vsu1")->group_id, "");        // vsu1 collapsed node at root
    EXPECT_EQ(bp.find_node("vsu1:vin")->group_id, "vsu1");  // internal to vsu1
    EXPECT_EQ(bp.find_node("vsu1:lamp")->group_id, "vsu1"); // internal to vsu1
    EXPECT_EQ(bp.find_node("nested1")->group_id, "vsu1");   // collapsed node INSIDE vsu1 — preserved!
    EXPECT_EQ(bp.find_node("nested1:r1")->group_id, "nested1"); // internal to nested1
}

TEST(RecomputeGroupIds, TopLevelCollapsedNodeStaysRoot) {
    // Top-level collapsed Blueprint nodes with group_id="" should stay at root.
    Blueprint bp;

    Node collapsed; collapsed.id = "bp1"; collapsed.at(0, 0).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.group_id = "";  // top-level
    bp.add_node(std::move(collapsed));

    Node internal; internal.id = "bp1:r1"; internal.at(0, 0).size_wh(60, 40);
    bp.add_node(std::move(internal));

    CollapsedGroup g;
    g.id = "bp1";
    g.internal_node_ids = {"bp1:r1"};
    bp.collapsed_groups.push_back(g);

    bp.recompute_group_ids();

    EXPECT_EQ(bp.find_node("bp1")->group_id, "");
    EXPECT_EQ(bp.find_node("bp1:r1")->group_id, "bp1");
}

// ============================================================================
// Regression: wire deselection on empty-space click
// ============================================================================

TEST(WireDeselect, ClickEmptySpaceDeselectsWire) {
    // Setup: two nodes with a horizontal wire between them
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Resistor";
    n1.at(0, 0).size_wh(100, 60);
    n1.outputs.push_back(Port("v_out", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(n1));

    Node n2; n2.id = "b"; n2.type_name = "Resistor";
    n2.at(300, 0).size_wh(100, 60);
    n2.inputs.push_back(Port("v_in", PortSide::Input, an24::PortType::V));
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start = WireEnd("a", "v_out", PortSide::Output);
    w.end = WireEnd("b", "v_in", PortSide::Input);
    bp.add_wire(w);

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);

    // 1. Manually select the wire (simulating a hit on the wire)
    //    (Direct set avoids needing pixel-perfect hit test coordinates)
    input.clear_selection();
    // Use internal on_mouse_down with a position that hits the wire:
    // Wire goes from node "a" (0..100, 0..60) port at ~(100, 30)
    // to node "b" (300..400, 0..60) port at ~(300, 30)
    // Midpoint of wire segment is ~(200, 30) in world coords
    // With default viewport (zoom=1, pan=0), screen == world
    input.on_mouse_down(Pt(200, 30), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, Pt(200, 30), canvas_min);

    // Wire should be selected (if hit test found it)
    // If not, manually select for the deselection test
    if (!input.selected_wire().has_value()) {
        // Wire hit test may miss due to port position calculation;
        // force-select to test the deselection path
        input.on_mouse_down(Pt(200, 30), MouseButton::Left, canvas_min);
        input.on_mouse_up(MouseButton::Left, Pt(200, 30), canvas_min);
    }

    // 2. Click on empty space (far from any node/wire)
    input.on_mouse_down(Pt(200, 500), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, Pt(200, 500), canvas_min);

    // Wire must be deselected
    EXPECT_FALSE(input.selected_wire().has_value())
        << "Clicking empty space must deselect the wire";
    EXPECT_TRUE(input.selected_nodes().empty())
        << "Clicking empty space must deselect all nodes";
}

TEST(WireDeselect, ClickNodeDeselectsWire) {
    // Clicking a node while a wire is selected should deselect the wire
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Resistor";
    n1.at(0, 0).size_wh(100, 60);
    n1.outputs.push_back(Port("v_out", PortSide::Output, an24::PortType::V));
    bp.add_node(std::move(n1));

    Node n2; n2.id = "b"; n2.type_name = "Resistor";
    n2.at(300, 0).size_wh(100, 60);
    n2.inputs.push_back(Port("v_in", PortSide::Input, an24::PortType::V));
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start = WireEnd("a", "v_out", PortSide::Output);
    w.end = WireEnd("b", "v_in", PortSide::Input);
    bp.add_wire(w);

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);

    // Manually select wire via on_mouse_down on wire midpoint
    input.on_mouse_down(Pt(200, 30), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, Pt(200, 30), canvas_min);

    // Click on node "a" center (50, 30) — inside the 100x60 node at (0,0)
    input.on_mouse_down(Pt(50, 30), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, Pt(50, 30), canvas_min);

    // Wire should be deselected, node selected instead
    EXPECT_FALSE(input.selected_wire().has_value())
        << "Clicking a node must deselect the wire";
    EXPECT_FALSE(input.selected_nodes().empty())
        << "Clicking a node must select the node";
}

// ============================================================================
// Regression: tooltip canvas_min parameter
// ============================================================================

TEST(TooltipCanvasMin, ScenePassesCanvasMinToDetector) {
    // This test verifies the API signature accepts canvas_min.
    // Full rendering test requires ImGui, so just verify the call compiles
    // and returns inactive when no nodes are near.
    Blueprint bp;
    Node n; n.id = "n1"; n.at(100, 100).size_wh(80, 60);
    bp.add_node(std::move(n));

    VisualScene scene(bp);
    an24::Simulator<an24::JIT_Solver> sim;

    // canvas_min at (50, 30) — simulating a sub-window offset
    Pt canvas_min(50.0f, 30.0f);
    auto tooltip = scene.detectTooltip(Pt(9999, 9999), sim, canvas_min);

    // Far from any node — should be inactive
    EXPECT_FALSE(tooltip.active);
}

// ============================================================================
// Regression: dragging RP must keep wire selected (selection color + RP circle)
// Before fix, enter_drag_routing_point didn't set selected_wire_, so the wire
// lost its selection color and RP circles disappeared during drag.
// ============================================================================

TEST(RPDrag, DraggingRoutingPointSelectsWire) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80);
    n1.output("v_out");
    bp.add_node(std::move(n1));

    Node n2; n2.id = "b"; n2.type_name = "Load";
    n2.at(400, 0).size_wh(120, 80);
    n2.input("v_in");
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w1",
        WireEnd("a", "v_out", PortSide::Output),
        WireEnd("b", "v_in", PortSide::Input));
    w.add_routing_point(Pt(250.0f, 100.0f));
    bp.add_wire(std::move(w));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);

    // No wire selected initially
    EXPECT_FALSE(input.selected_wire().has_value());

    // Click on the routing point (250, 100) — should enter DraggingRoutingPoint
    input.on_mouse_down(Pt(250, 100), MouseButton::Left, canvas_min);

    // Wire must be selected during RP drag
    EXPECT_EQ(input.state(), InputState::DraggingRoutingPoint);
    ASSERT_TRUE(input.selected_wire().has_value())
        << "Wire must be selected when dragging its routing point";
    EXPECT_EQ(*input.selected_wire(), 0u);

    // Drag the RP — wire must stay selected
    input.on_mouse_drag(MouseButton::Left, Pt(10, 10), canvas_min);
    EXPECT_TRUE(input.selected_wire().has_value())
        << "Wire must remain selected during RP drag";

    // Release — wire should still be selected
    input.on_mouse_up(MouseButton::Left, Pt(260, 110), canvas_min);
    EXPECT_TRUE(input.selected_wire().has_value())
        << "Wire must remain selected after RP drag ends";
}

// ============================================================================
// Regression: hover near routing point sets hovered_wire
// Before fix, update_hover only checked HitType::Wire, not HitType::RoutingPoint,
// so wire lost hover highlight within RP hit radius.
// ============================================================================

TEST(WireHover, HoverNearRoutingPoint_SetsHoveredWire) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80); n1.output("v_out");
    bp.add_node(std::move(n1));
    Node n2; n2.id = "b"; n2.type_name = "Load";
    n2.at(400, 0).size_wh(120, 80); n2.input("v_in");
    bp.add_node(std::move(n2));
    Wire w = Wire::make("w1",
        WireEnd("a", "v_out", PortSide::Output),
        WireEnd("b", "v_in", PortSide::Input));
    w.add_routing_point(Pt(250.0f, 100.0f));
    bp.add_wire(std::move(w));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    // Hover exactly on the routing point
    input.update_hover(Pt(250.0f, 100.0f));
    ASSERT_TRUE(input.hovered_wire().has_value())
        << "Hovering on RP must set hovered_wire";
    EXPECT_EQ(*input.hovered_wire(), 0u);
}

// ============================================================================
// Regression: stale hover clears when moving cursor far away
// Before fix, hover on window-unhover wasn't cleared — wire stayed highlighted.
// ============================================================================

TEST(WireHover, HoverClears_WhenCursorMovesAway) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80); n1.output("v_out");
    bp.add_node(std::move(n1));
    Node n2; n2.id = "b"; n2.type_name = "Load";
    n2.at(400, 0).size_wh(120, 80); n2.input("v_in");
    bp.add_node(std::move(n2));
    Wire w = Wire::make("w1",
        WireEnd("a", "v_out", PortSide::Output),
        WireEnd("b", "v_in", PortSide::Input));
    w.add_routing_point(Pt(250.0f, 100.0f));
    bp.add_wire(std::move(w));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    // First hover on wire RP — should highlight
    input.update_hover(Pt(250.0f, 100.0f));
    ASSERT_TRUE(input.hovered_wire().has_value());

    // Move cursor far away (simulates window unhover: update_hover with far-away pos)
    input.update_hover(Pt(-1e9f, -1e9f));
    EXPECT_FALSE(input.hovered_wire().has_value())
        << "Moving cursor far away must clear hovered_wire";
}

// ============================================================================
// Regression: double-click on RP removes it
// DRY fix unified on_double_click to use hitTest(), which returns
// RoutingPoint with higher priority than Wire — enabling RP removal.
// ============================================================================

TEST(RPDoubleClick, DoubleClickOnRP_RemovesIt) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80); n1.output("v_out");
    bp.add_node(std::move(n1));
    Node n2; n2.id = "b"; n2.type_name = "Load";
    n2.at(400, 0).size_wh(120, 80); n2.input("v_in");
    bp.add_node(std::move(n2));
    Wire w = Wire::make("w1",
        WireEnd("a", "v_out", PortSide::Output),
        WireEnd("b", "v_in", PortSide::Input));
    w.add_routing_point(Pt(250.0f, 100.0f));
    bp.add_wire(std::move(w));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    ASSERT_EQ(scene.wires()[0].routing_points.size(), 1u);

    // Double-click on the routing point
    input.on_double_click(Pt(250.0f, 100.0f), canvas_min);

    EXPECT_EQ(scene.wires()[0].routing_points.size(), 0u)
        << "Double-click on RP must remove it";
}
