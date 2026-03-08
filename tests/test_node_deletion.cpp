#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/visual/scene/scene.h"
#include "editor/visual/scene/persist.h"
#include "editor/input/canvas_input.h"
#include "editor/visual/scene/wire_manager.h"

// ============================================================================
// Node Deletion Tests (TDD)
// ============================================================================

// Helper: build a small blueprint with 3 nodes and 2 wires
static Blueprint make_three_node_bp() {
    Blueprint bp;
    Node a; a.id = "a"; a.at(0, 0).size_wh(120, 80);
    a.output("out");
    bp.add_node(std::move(a));

    Node b; b.id = "b"; b.at(200, 0).size_wh(120, 80);
    b.input("in").output("out");
    bp.add_node(std::move(b));

    Node c; c.id = "c"; c.at(400, 0).size_wh(120, 80);
    c.input("in");
    bp.add_node(std::move(c));

    bp.add_wire(Wire::make("w1", wire_output("a", "out"), wire_input("b", "in")));
    bp.add_wire(Wire::make("w2", wire_output("b", "out"), wire_input("c", "in")));
    return bp;
}

// ---- 1. Single node deletion removes connected wires ----

TEST(NodeDeletion, RemoveNode_RemovesConnectedWires) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);

    scene.removeNode(1);  // remove "b"
    EXPECT_EQ(bp.nodes.size(), 2u);
    EXPECT_EQ(bp.wires.size(), 0u);  // both w1 and w2 reference "b"
}

// ---- 2. Batch deletion removes all connected wires ----

TEST(NodeDeletion, RemoveNodes_BatchRemovesWires) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);

    // Delete "a" (index 0) and "c" (index 2) — sorted desc
    std::vector<size_t> indices = {2, 0};
    scene.removeNodes(indices);

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, "b");
    EXPECT_EQ(bp.wires.size(), 0u);  // both wires touched a or c
}

// ---- 3. Routing points are removed with the wire ----

TEST(NodeDeletion, RemoveNode_RoutingPointsDeletedWithWire) {
    Blueprint bp;
    Node a; a.id = "a"; a.at(0, 0).size_wh(120, 80); a.output("out");
    Node b; b.id = "b"; b.at(400, 0).size_wh(120, 80); b.input("in");
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w = Wire::make("w1", wire_output("a", "out"), wire_input("b", "in"));
    w.add_routing_point(Pt(150, 50)).add_routing_point(Pt(250, -20));
    bp.add_wire(std::move(w));
    ASSERT_EQ(bp.wires[0].routing_points.size(), 2u);

    VisualScene scene(bp);
    scene.removeNode(0);  // remove "a"

    EXPECT_EQ(bp.wires.size(), 0u);  // wire + its routing points gone
}

// ---- 4. Unrelated wires survive deletion ----

TEST(NodeDeletion, RemoveNode_UnrelatedWiresSurvive) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);

    scene.removeNode(0);  // remove "a"
    EXPECT_EQ(bp.nodes.size(), 2u);
    // w1 (a→b) removed, w2 (b→c) survives
    ASSERT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wires[0].id, "w2");
}

// ---- 5. Deletion of node inside a collapsed group ----

TEST(NodeDeletion, RemoveNode_InGroup_CleansInternalNodeIds) {
    Blueprint bp;
    Node root; root.id = "lamp1"; root.kind = NodeKind::Blueprint;
    root.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(root));

    Node n1; n1.id = "lamp1:led"; n1.group_id = "lamp1";
    n1.at(0, 0).size_wh(80, 60); n1.input("in").output("out");
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1:res"; n2.group_id = "lamp1";
    n2.at(200, 0).size_wh(80, 60); n2.input("in");
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make("gw1", wire_output("lamp1:led", "out"),
                            wire_input("lamp1:res", "in")));

    CollapsedGroup g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:led", "lamp1:res"};
    bp.collapsed_groups.push_back(g);

    VisualScene scene(bp, "lamp1");  // sub-window for this group

    // Find index of "lamp1:led" in bp.nodes
    size_t led_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1:led") { led_idx = i; break; }
    ASSERT_NE(led_idx, SIZE_MAX);

    scene.removeNodes({led_idx});

    // Node gone, wire gone
    EXPECT_EQ(bp.find_node("lamp1:led"), nullptr);
    EXPECT_EQ(bp.wires.size(), 0u);

    // internal_node_ids updated
    ASSERT_EQ(bp.collapsed_groups.size(), 1u);
    auto& ids = bp.collapsed_groups[0].internal_node_ids;
    EXPECT_EQ(std::count(ids.begin(), ids.end(), "lamp1:led"), 0);
    EXPECT_EQ(std::count(ids.begin(), ids.end(), "lamp1:res"), 1);
}

// ---- 6. Backspace key triggers deletion (same as Delete) ----

TEST(NodeDeletion, Backspace_DeletesSelectedNodes) {
    Blueprint bp;
    Node a; a.id = "a"; a.at(0, 0).size_wh(120, 80); a.output("out");
    Node b; b.id = "b"; b.at(200, 0).size_wh(120, 80); b.input("in");
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make("w1", wire_output("a", "out"), wire_input("b", "in")));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    // Select node 0 ("a")
    input.add_node_selection(0);
    ASSERT_EQ(input.selected_nodes().size(), 1u);

    InputResult result = input.on_key(Key::Backspace);

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, "b");
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(input.selected_nodes().empty());
    EXPECT_TRUE(result.rebuild_simulation);
}

// ---- 7. Delete key still works ----

TEST(NodeDeletion, Delete_DeletesSelectedNodes) {
    Blueprint bp;
    Node a; a.id = "a"; a.at(0, 0).size_wh(120, 80); a.output("out");
    Node b; b.id = "b"; b.at(200, 0).size_wh(120, 80); b.input("in");
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make("w1", wire_output("a", "out"), wire_input("b", "in")));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    input.add_node_selection(0);
    InputResult result = input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(result.rebuild_simulation);
}

// ---- 8. Multi-select + delete ----

TEST(NodeDeletion, MultiSelect_DeleteAll) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    input.add_node_selection(0);
    input.add_node_selection(1);
    input.add_node_selection(2);

    input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 0u);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(input.selected_nodes().empty());
}

// ---- 9. Delete with nothing selected is a no-op ----

TEST(NodeDeletion, DeleteWithNoSelection_Noop) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    InputResult result = input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 3u);
    EXPECT_EQ(bp.wires.size(), 2u);
    // No rebuild needed if nothing was deleted
    EXPECT_FALSE(result.rebuild_simulation);
}

// ---- 10. Persistence: deleted nodes don't appear in saved JSON ----

TEST(NodeDeletion, Persist_DeletedNodeNotInJson) {
    Blueprint bp = make_three_node_bp();
    VisualScene scene(bp);

    scene.removeNode(1);  // remove "b"

    std::string json = blueprint_to_editor_json(bp);

    // Parse and verify
    auto loaded = blueprint_from_json(json);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->nodes.size(), 2u);
    EXPECT_EQ(loaded->wires.size(), 0u);
    EXPECT_EQ(loaded->find_node("b"), nullptr);
}

// ---- 11. Persistence: stale wires (referencing nonexistent nodes) cleaned on save ----

TEST(NodeDeletion, Persist_StaleWiresCleanedOnSave) {
    Blueprint bp;
    Node a; a.id = "a"; a.at(0, 0).size_wh(120, 80); a.output("out");
    Node b; b.id = "b"; b.at(200, 0).size_wh(120, 80); b.input("in");
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make("w1", wire_output("a", "out"), wire_input("b", "in")));

    // Manually corrupt: add a stale wire referencing nonexistent node
    bp.add_wire(Wire::make("stale", wire_output("ghost", "p"), wire_input("b", "in")));
    ASSERT_EQ(bp.wires.size(), 2u);

    std::string json = blueprint_to_editor_json(bp);
    auto loaded = blueprint_from_json(json);
    ASSERT_TRUE(loaded.has_value());

    // The stale wire should NOT survive the roundtrip
    for (const auto& w : loaded->wires) {
        EXPECT_NE(w.start.node_id, "ghost")
            << "Stale wire referencing nonexistent node 'ghost' should be cleaned";
    }
}

// ---- 12. Deletion in sub-window via CanvasInput ----

TEST(NodeDeletion, SubWindowDeletion_ViaCanvasInput) {
    Blueprint bp;
    // Root node (collapsed blueprint)
    Node root; root.id = "lamp1"; root.kind = NodeKind::Blueprint;
    root.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(root));

    // Internal nodes
    Node n1; n1.id = "lamp1:a"; n1.group_id = "lamp1";
    n1.at(0, 0).size_wh(80, 60); n1.output("out");
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1:b"; n2.group_id = "lamp1";
    n2.at(200, 0).size_wh(80, 60); n2.input("in");
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make("gw", wire_output("lamp1:a", "out"),
                            wire_input("lamp1:b", "in")));

    CollapsedGroup g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:a", "lamp1:b"};
    bp.collapsed_groups.push_back(g);

    // Sub-window scene for lamp1
    VisualScene sub_scene(bp, "lamp1");
    WireManager wm(sub_scene);
    CanvasInput sub_input(sub_scene, wm);

    // Find index of "lamp1:a"
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1:a") { idx = i; break; }
    ASSERT_NE(idx, SIZE_MAX);

    sub_input.add_node_selection(idx);
    InputResult result = sub_input.on_key(Key::Backspace);

    EXPECT_TRUE(result.rebuild_simulation);
    EXPECT_EQ(bp.find_node("lamp1:a"), nullptr);
    EXPECT_EQ(bp.wires.size(), 0u);

    // internal_node_ids cleaned
    auto& ids = bp.collapsed_groups[0].internal_node_ids;
    EXPECT_EQ(std::count(ids.begin(), ids.end(), "lamp1:a"), 0);
}
