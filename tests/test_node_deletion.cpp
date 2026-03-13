#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/visual/scene.h"
#include "editor/visual/scene_mutations.h"
#include "editor/visual/persist.h"
#include "editor/input/canvas_input.h"
#include "editor/window/window_manager.h"
#include "editor/viewport/viewport.h"

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

// Helper: create a visual::Scene from a Blueprint and rebuild
static visual::Scene make_scene(const Blueprint& bp, const std::string& group_id = "") {
    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, group_id);
    return scene;
}

// ---- 1. Single node deletion removes connected wires ----

TEST(NodeDeletion, RemoveNode_RemovesConnectedWires) {
    Blueprint bp = make_three_node_bp();
    auto scene = make_scene(bp);

    visual::mutations::remove_nodes(scene, bp, {1});  // remove "b"
    EXPECT_EQ(bp.nodes.size(), 2u);
    EXPECT_EQ(bp.wires.size(), 0u);  // both w1 and w2 reference "b"
}

// ---- 2. Batch deletion removes all connected wires ----

TEST(NodeDeletion, RemoveNodes_BatchRemovesWires) {
    Blueprint bp = make_three_node_bp();
    auto scene = make_scene(bp);

    // Delete "a" (index 0) and "c" (index 2) — sorted desc
    std::vector<size_t> indices = {2, 0};
    visual::mutations::remove_nodes(scene, bp, indices);

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

    auto scene = make_scene(bp);
    visual::mutations::remove_nodes(scene, bp, {0});  // remove "a"

    EXPECT_EQ(bp.wires.size(), 0u);  // wire + its routing points gone
}

// ---- 4. Unrelated wires survive deletion ----

TEST(NodeDeletion, RemoveNode_UnrelatedWiresSurvive) {
    Blueprint bp = make_three_node_bp();
    auto scene = make_scene(bp);

    visual::mutations::remove_nodes(scene, bp, {0});  // remove "a"
    EXPECT_EQ(bp.nodes.size(), 2u);
    // w1 (a→b) removed, w2 (b→c) survives
    ASSERT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wires[0].id, "w2");
}

// ---- 5. Deletion of node inside a collapsed group ----

TEST(NodeDeletion, RemoveNode_InGroup_CleansInternalNodeIds) {
    Blueprint bp;
    Node root; root.id = "lamp1"; root.expandable = true;
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

    SubBlueprintInstance g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:led", "lamp1:res"};
    bp.sub_blueprint_instances.push_back(g);

    auto scene = make_scene(bp, "lamp1");  // sub-window for this group

    // Find index of "lamp1:led" in bp.nodes
    size_t led_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1:led") { led_idx = i; break; }
    ASSERT_NE(led_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {led_idx});

    // Node gone, wire gone
    EXPECT_EQ(bp.find_node("lamp1:led"), nullptr);
    EXPECT_EQ(bp.wires.size(), 0u);

    // internal_node_ids updated
    ASSERT_EQ(bp.sub_blueprint_instances.size(), 1u);
    auto& ids = bp.sub_blueprint_instances[0].internal_node_ids;
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

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    // Select node "a" widget
    auto* widget_a = scene.find("a");
    ASSERT_NE(widget_a, nullptr);
    input.add_node_selection(widget_a);
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

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    auto* widget_a = scene.find("a");
    ASSERT_NE(widget_a, nullptr);
    input.add_node_selection(widget_a);
    InputResult result = input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(result.rebuild_simulation);
}

// ---- 8. Multi-select + delete ----

TEST(NodeDeletion, MultiSelect_DeleteAll) {
    Blueprint bp = make_three_node_bp();
    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    input.add_node_selection(scene.find("a"));
    input.add_node_selection(scene.find("b"));
    input.add_node_selection(scene.find("c"));

    input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 0u);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(input.selected_nodes().empty());
}

// ---- 9. Delete with nothing selected is a no-op ----

TEST(NodeDeletion, DeleteWithNoSelection_Noop) {
    Blueprint bp = make_three_node_bp();
    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    InputResult result = input.on_key(Key::Delete);

    EXPECT_EQ(bp.nodes.size(), 3u);
    EXPECT_EQ(bp.wires.size(), 2u);
    // No rebuild needed if nothing was deleted
    EXPECT_FALSE(result.rebuild_simulation);
}

// ---- 10. Persistence: deleted nodes don't appear in saved JSON ----

TEST(NodeDeletion, Persist_DeletedNodeNotInJson) {
    Blueprint bp = make_three_node_bp();
    auto scene = make_scene(bp);

    visual::mutations::remove_nodes(scene, bp, {1});  // remove "b"

    std::string json = bp.serialize();

    // Parse and verify
    auto loaded = Blueprint::deserialize(json);
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

    std::string json = bp.serialize();
    auto loaded = Blueprint::deserialize(json);
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
    Node root; root.id = "lamp1"; root.expandable = true;
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

    SubBlueprintInstance g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:a", "lamp1:b"};
    bp.sub_blueprint_instances.push_back(g);

    // Sub-window scene for lamp1
    visual::Scene sub_scene;
    Viewport vp;
    std::string sub_group_id = "lamp1";
    visual::mutations::rebuild(sub_scene, bp, sub_group_id);
    CanvasInput sub_input(sub_scene, vp, bp, sub_group_id);

    // Select "lamp1:a" widget
    auto* widget = sub_scene.find("lamp1:a");
    ASSERT_NE(widget, nullptr);
    sub_input.add_node_selection(widget);
    InputResult result = sub_input.on_key(Key::Backspace);

    EXPECT_TRUE(result.rebuild_simulation);
    EXPECT_EQ(bp.find_node("lamp1:a"), nullptr);
    EXPECT_EQ(bp.wires.size(), 0u);

    // internal_node_ids cleaned
    auto& ids = bp.sub_blueprint_instances[0].internal_node_ids;
    EXPECT_EQ(std::count(ids.begin(), ids.end(), "lamp1:a"), 0);
}

// ============================================================================
// Recursive sub-blueprint deletion regression tests
// ============================================================================

// Helper: build blueprint with sub-blueprint "lamp1" containing 2 internal nodes + 1 wire
static Blueprint make_bp_with_sub_blueprint() {
    Blueprint bp;

    // Root-level nodes
    Node bat; bat.id = "bat"; bat.type_name = "Battery"; bat.at(0, 0).size_wh(120, 80);
    bat.output("v_out");
    bp.add_node(std::move(bat));

    // Collapsed blueprint node
    Node lamp; lamp.id = "lamp1"; lamp.type_name = "lamp"; lamp.expandable = true;
    lamp.at(200, 0).size_wh(120, 80);
    lamp.input("vin").output("vout");
    bp.add_node(std::move(lamp));

    // Internal nodes of lamp1
    Node led; led.id = "lamp1:led"; led.type_name = "IndicatorLight"; led.group_id = "lamp1";
    led.at(0, 0).size_wh(80, 60); led.input("in").output("out");
    bp.add_node(std::move(led));

    Node res; res.id = "lamp1:res"; res.type_name = "Resistor"; res.group_id = "lamp1";
    res.at(200, 0).size_wh(80, 60); res.input("in").output("out");
    bp.add_node(std::move(res));

    // External wire: bat -> lamp1
    bp.add_wire(Wire::make("ext_w", wire_output("bat", "v_out"), wire_input("lamp1", "vin")));
    // Internal wire: led -> res
    bp.add_wire(Wire::make("int_w", wire_output("lamp1:led", "out"), wire_input("lamp1:res", "in")));

    SubBlueprintInstance g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:led", "lamp1:res"};
    bp.sub_blueprint_instances.push_back(g);

    return bp;
}

// ---- 13. Deleting sub-blueprint removes internal nodes ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesInternalNodes) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto scene = make_scene(bp);

    // Find lamp1 index
    size_t lamp_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1") { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    // lamp1 node gone
    EXPECT_EQ(bp.find_node("lamp1"), nullptr);
    // Internal nodes gone
    EXPECT_EQ(bp.find_node("lamp1:led"), nullptr);
    EXPECT_EQ(bp.find_node("lamp1:res"), nullptr);
    // Only "bat" remains
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, "bat");
}

// ---- 14. Deleting sub-blueprint removes internal wires ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesInternalWires) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto scene = make_scene(bp);

    size_t lamp_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1") { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    // Both ext_w (bat->lamp1) and int_w (led->res) should be gone
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(bp.wire_index_.size(), 0u);
}

// ---- 15. Deleting sub-blueprint removes SubBlueprintInstance entry ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesSubBlueprintInstance) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto scene = make_scene(bp);

    size_t lamp_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1") { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    EXPECT_EQ(bp.sub_blueprint_instances.size(), 0u)
        << "SubBlueprintInstance for deleted sub-blueprint should be removed";
}

// ---- 16. Recursive: sub-sub-blueprint deletion ----

TEST(NodeDeletion, DeleteSubBlueprint_Recursive_SubSubBlueprint) {
    Blueprint bp;

    // Root node
    Node root; root.id = "top"; root.at(0, 0).size_wh(120, 80); root.output("out");
    bp.add_node(std::move(root));

    // Level 1: sub-blueprint "sys1"
    Node sys1; sys1.id = "sys1"; sys1.expandable = true;
    sys1.at(200, 0).size_wh(120, 80); sys1.input("in");
    bp.add_node(std::move(sys1));

    // Level 2: inside sys1, another sub-blueprint "sys1:sub2"
    Node sub2; sub2.id = "sys1:sub2"; sub2.expandable = true;
    sub2.group_id = "sys1";
    sub2.at(0, 0).size_wh(100, 60); sub2.input("in");
    bp.add_node(std::move(sub2));

    // Level 3: inside sys1:sub2, a leaf node
    Node leaf; leaf.id = "sys1:sub2:leaf"; leaf.type_name = "Resistor";
    leaf.group_id = "sys1:sub2";
    leaf.at(0, 0).size_wh(80, 60); leaf.input("in");
    bp.add_node(std::move(leaf));

    // Wires at each level
    bp.add_wire(Wire::make("w0", wire_output("top", "out"), wire_input("sys1", "in")));
    bp.add_wire(Wire::make("w1", wire_output("sys1", "in"), wire_input("sys1:sub2", "in")));
    bp.add_wire(Wire::make("w2", wire_output("sys1:sub2", "in"), wire_input("sys1:sub2:leaf", "in")));

    // Sub-blueprint instances
    SubBlueprintInstance g1("sys1", "blueprints/sys1.json", "sys1");
    g1.internal_node_ids = {"sys1:sub2"};
    g1.baked_in = true;
    bp.sub_blueprint_instances.push_back(g1);

    SubBlueprintInstance g2("sys1:sub2", "blueprints/sub2.json", "sub2");
    g2.internal_node_ids = {"sys1:sub2:leaf"};
    g2.baked_in = true;
    bp.sub_blueprint_instances.push_back(g2);

    ASSERT_EQ(bp.nodes.size(), 4u);
    ASSERT_EQ(bp.wires.size(), 3u);
    ASSERT_EQ(bp.sub_blueprint_instances.size(), 2u);

    auto scene = make_scene(bp);

    // Delete sys1 — should cascade to sys1:sub2 and sys1:sub2:leaf
    size_t sys1_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "sys1") { sys1_idx = i; break; }
    ASSERT_NE(sys1_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {sys1_idx});

    // Only "top" remains
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, "top");
    // All wires gone
    EXPECT_EQ(bp.wires.size(), 0u);
    // All collapsed groups gone
    EXPECT_EQ(bp.sub_blueprint_instances.size(), 0u);
}

// ---- 17. WindowManager removes orphaned sub-windows ----

TEST(NodeDeletion, WindowManager_RemovesOrphanedWindows) {
    Blueprint bp = make_bp_with_sub_blueprint();
    WindowManager wm(bp);
    EXPECT_EQ(wm.count(), 1u);  // root only

    // Open sub-window for lamp1
    wm.open("lamp1", "lamp [lamp1]");
    EXPECT_EQ(wm.count(), 2u);

    // Delete lamp1 via root scene
    auto scene = make_scene(bp);
    size_t lamp_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "lamp1") { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});
    wm.removeOrphanedWindows();

    EXPECT_EQ(wm.count(), 1u) << "Sub-window for deleted group should be closed";
    EXPECT_EQ(wm.find("lamp1"), nullptr);
}

// ---- 18. Deleting regular node does NOT trigger recursive cleanup ----

TEST(NodeDeletion, DeleteRegularNode_NoRecursiveEffect) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto scene = make_scene(bp);

    // Delete "bat" (regular node, not a Blueprint)
    size_t bat_idx = SIZE_MAX;
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == "bat") { bat_idx = i; break; }
    ASSERT_NE(bat_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {bat_idx});

    // bat gone, ext_w gone, but lamp1 and internals survive
    EXPECT_EQ(bp.find_node("bat"), nullptr);
    EXPECT_NE(bp.find_node("lamp1"), nullptr);
    EXPECT_NE(bp.find_node("lamp1:led"), nullptr);
    EXPECT_NE(bp.find_node("lamp1:res"), nullptr);
    EXPECT_EQ(bp.sub_blueprint_instances.size(), 1u);
    // Only internal wire survives
    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wires[0].id, "int_w");
}
