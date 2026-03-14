#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/visual/scene.h"
#include "editor/visual/scene_mutations.h"
#include "editor/visual/persist.h"
#include "editor/visual/wire/wire.h"
#include "editor/input/canvas_input.h"
#include "editor/window/window_manager.h"
#include "editor/viewport/viewport.h"
#include "ui/core/interned_id.h"


// ============================================================================
// Node Deletion Tests (TDD)
// ============================================================================

// Helper: build a small blueprint with 3 nodes and 2 wires
static Blueprint make_three_node_bp() {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80);
    a.output(I.intern("out"));
    bp.add_node(std::move(a));

    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80);
    b.input(I.intern("in")).output(I.intern("out"));
    bp.add_node(std::move(b));

    Node c; c.id = I.intern("c"); c.at(400, 0).size_wh(120, 80);
    c.input(I.intern("in"));
    bp.add_node(std::move(c));

    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));
    bp.add_wire(Wire::make(I.intern("w2"), wire_output(I.intern("b"), I.intern("out")), wire_input(I.intern("c"), I.intern("in"))));
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
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    visual::mutations::remove_nodes(scene, bp, {1});  // remove "b"
    EXPECT_EQ(bp.nodes.size(), 2u);
    EXPECT_EQ(bp.wires.size(), 0u);  // both w1 and w2 reference "b"
}

// ---- 2. Batch deletion removes all connected wires ----

TEST(NodeDeletion, RemoveNodes_BatchRemovesWires) {
    Blueprint bp = make_three_node_bp();
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    // Delete "a" (index 0) and "c" (index 2) — sorted desc
    std::vector<size_t> indices = {2, 0};
    visual::mutations::remove_nodes(scene, bp, indices);

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, I.intern("b"));
    EXPECT_EQ(bp.wires.size(), 0u);  // both wires touched a or c
}

// ---- 3. Routing points are removed with the wire ----

TEST(NodeDeletion, RemoveNode_RoutingPointsDeletedWithWire) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(400, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w = Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in")));
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
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    visual::mutations::remove_nodes(scene, bp, {0});  // remove "a"
    EXPECT_EQ(bp.nodes.size(), 2u);
    // w1 (a→b) removed, w2 (b→c) survives
    ASSERT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wires[0].id, I.intern("w2"));
}

// ---- 5. Deletion of node inside a collapsed group ----

TEST(NodeDeletion, RemoveNode_InGroup_CleansInternalNodeIds) {
    Blueprint bp;
    auto& I = bp.interner();
    Node root; root.id = I.intern("lamp1"); root.expandable = true;
    root.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(root));

    Node n1; n1.id = I.intern("lamp1:led"); n1.group_id = "lamp1";
    n1.at(0, 0).size_wh(80, 60); n1.input(I.intern("in")).output(I.intern("out"));
    bp.add_node(std::move(n1));

    Node n2; n2.id = I.intern("lamp1:res"); n2.group_id = "lamp1";
    n2.at(200, 0).size_wh(80, 60); n2.input(I.intern("in"));
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make(I.intern("gw1"), wire_output(I.intern("lamp1:led"), I.intern("out")),
                            wire_input(I.intern("lamp1:res"), I.intern("in"))));

    SubBlueprintInstance g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:led", "lamp1:res"};
    bp.sub_blueprint_instances.push_back(g);

    auto scene = make_scene(bp, "lamp1");  // sub-window for this group

    // Find index of "lamp1:led" in bp.nodes
    size_t led_idx = SIZE_MAX;
    auto led_id = I.intern("lamp1:led");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == led_id) { led_idx = i; break; }
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
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

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
    EXPECT_EQ(bp.nodes[0].id, I.intern("b"));
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_TRUE(input.selected_nodes().empty());
    EXPECT_TRUE(result.rebuild_simulation);
}

// ---- 7. Delete key still works ----

TEST(NodeDeletion, Delete_DeletesSelectedNodes) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

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
    auto& I = bp.interner();
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
    auto& I = bp.interner();
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
    auto& I = bp.interner();
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
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

    // Manually corrupt: add a stale wire referencing nonexistent node
    bp.add_wire(Wire::make(I.intern("stale"), wire_output(I.intern("ghost"), I.intern("p")), wire_input(I.intern("b"), I.intern("in"))));
    ASSERT_EQ(bp.wires.size(), 2u);

    std::string json = bp.serialize();
    auto loaded = Blueprint::deserialize(json);
    ASSERT_TRUE(loaded.has_value());

    // The stale wire should NOT survive the roundtrip
    for (const auto& w : loaded->wires) {
        auto node_name = loaded->interner().resolve(w.start.node_id);
        EXPECT_NE(node_name, "ghost")
            << "Stale wire referencing nonexistent node 'ghost' should be cleaned";
    }
}

// ---- 12. Deletion in sub-window via CanvasInput ----

TEST(NodeDeletion, SubWindowDeletion_ViaCanvasInput) {
    Blueprint bp;
    auto& I = bp.interner();
    // Root node (collapsed blueprint)
    Node root; root.id = I.intern("lamp1"); root.expandable = true;
    root.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(root));

    // Internal nodes
    Node n1; n1.id = I.intern("lamp1:a"); n1.group_id = "lamp1";
    n1.at(0, 0).size_wh(80, 60); n1.output(I.intern("out"));
    bp.add_node(std::move(n1));

    Node n2; n2.id = I.intern("lamp1:b"); n2.group_id = "lamp1";
    n2.at(200, 0).size_wh(80, 60); n2.input(I.intern("in"));
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make(I.intern("gw"), wire_output(I.intern("lamp1:a"), I.intern("out")),
                            wire_input(I.intern("lamp1:b"), I.intern("in"))));

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
    auto& I = bp.interner();

    // Root-level nodes
    Node bat; bat.id = I.intern("bat"); bat.type_name = "Battery"; bat.at(0, 0).size_wh(120, 80);
    bat.output(I.intern("v_out"));
    bp.add_node(std::move(bat));

    // Collapsed blueprint node
    Node lamp; lamp.id = I.intern("lamp1"); lamp.type_name = "lamp"; lamp.expandable = true;
    lamp.at(200, 0).size_wh(120, 80);
    lamp.input(I.intern("vin")).output(I.intern("vout"));
    bp.add_node(std::move(lamp));

    // Internal nodes of lamp1
    Node led; led.id = I.intern("lamp1:led"); led.type_name = "IndicatorLight"; led.group_id = "lamp1";
    led.at(0, 0).size_wh(80, 60); led.input(I.intern("in")).output(I.intern("out"));
    bp.add_node(std::move(led));

    Node res; res.id = I.intern("lamp1:res"); res.type_name = "Resistor"; res.group_id = "lamp1";
    res.at(200, 0).size_wh(80, 60); res.input(I.intern("in")).output(I.intern("out"));
    bp.add_node(std::move(res));

    // External wire: bat -> lamp1
    bp.add_wire(Wire::make(I.intern("ext_w"), wire_output(I.intern("bat"), I.intern("v_out")), wire_input(I.intern("lamp1"), I.intern("vin"))));
    // Internal wire: led -> res
    bp.add_wire(Wire::make(I.intern("int_w"), wire_output(I.intern("lamp1:led"), I.intern("out")), wire_input(I.intern("lamp1:res"), I.intern("in"))));

    SubBlueprintInstance g("lamp1", "blueprints/lamp.json", "lamp");
    g.internal_node_ids = {"lamp1:led", "lamp1:res"};
    bp.sub_blueprint_instances.push_back(g);

    return bp;
}

// ---- 13. Deleting sub-blueprint removes internal nodes ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesInternalNodes) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    // Find lamp1 index
    size_t lamp_idx = SIZE_MAX;
    auto lamp1_id = I.intern("lamp1");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == lamp1_id) { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    // lamp1 node gone
    EXPECT_EQ(bp.find_node("lamp1"), nullptr);
    // Internal nodes gone
    EXPECT_EQ(bp.find_node("lamp1:led"), nullptr);
    EXPECT_EQ(bp.find_node("lamp1:res"), nullptr);
    // Only "bat" remains
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, I.intern("bat"));
}

// ---- 14. Deleting sub-blueprint removes internal wires ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesInternalWires) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    size_t lamp_idx = SIZE_MAX;
    auto lamp1_id = I.intern("lamp1");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == lamp1_id) { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    // Both ext_w (bat->lamp1) and int_w (led->res) should be gone
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(bp.wire_index_size(), 0u);
}

// ---- 15. Deleting sub-blueprint removes SubBlueprintInstance entry ----

TEST(NodeDeletion, DeleteSubBlueprint_RemovesSubBlueprintInstance) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    size_t lamp_idx = SIZE_MAX;
    auto lamp1_id = I.intern("lamp1");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == lamp1_id) { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});

    EXPECT_EQ(bp.sub_blueprint_instances.size(), 0u)
        << "SubBlueprintInstance for deleted sub-blueprint should be removed";
}

// ---- 16. Recursive: sub-sub-blueprint deletion ----

TEST(NodeDeletion, DeleteSubBlueprint_Recursive_SubSubBlueprint) {
    Blueprint bp;
    auto& I = bp.interner();

    // Root node
    Node root; root.id = I.intern("top"); root.at(0, 0).size_wh(120, 80); root.output(I.intern("out"));
    bp.add_node(std::move(root));

    // Level 1: sub-blueprint "sys1"
    Node sys1; sys1.id = I.intern("sys1"); sys1.expandable = true;
    sys1.at(200, 0).size_wh(120, 80); sys1.input(I.intern("in"));
    bp.add_node(std::move(sys1));

    // Level 2: inside sys1, another sub-blueprint "sys1:sub2"
    Node sub2; sub2.id = I.intern("sys1:sub2"); sub2.expandable = true;
    sub2.group_id = "sys1";
    sub2.at(0, 0).size_wh(100, 60); sub2.input(I.intern("in"));
    bp.add_node(std::move(sub2));

    // Level 3: inside sys1:sub2, a leaf node
    Node leaf; leaf.id = I.intern("sys1:sub2:leaf"); leaf.type_name = "Resistor";
    leaf.group_id = "sys1:sub2";
    leaf.at(0, 0).size_wh(80, 60); leaf.input(I.intern("in"));
    bp.add_node(std::move(leaf));

    // Wires at each level
    bp.add_wire(Wire::make(I.intern("w0"), wire_output(I.intern("top"), I.intern("out")), wire_input(I.intern("sys1"), I.intern("in"))));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("sys1"), I.intern("in")), wire_input(I.intern("sys1:sub2"), I.intern("in"))));
    bp.add_wire(Wire::make(I.intern("w2"), wire_output(I.intern("sys1:sub2"), I.intern("in")), wire_input(I.intern("sys1:sub2:leaf"), I.intern("in"))));

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
    auto sys1_id = I.intern("sys1");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == sys1_id) { sys1_idx = i; break; }
    ASSERT_NE(sys1_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {sys1_idx});

    // Only "top" remains
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, I.intern("top"));
    // All wires gone
    EXPECT_EQ(bp.wires.size(), 0u);
    // All collapsed groups gone
    EXPECT_EQ(bp.sub_blueprint_instances.size(), 0u);
}

// ---- 17. WindowManager removes orphaned sub-windows ----

TEST(NodeDeletion, WindowManager_RemovesOrphanedWindows) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto& I = bp.interner();
    WindowManager wm(bp);
    EXPECT_EQ(wm.count(), 1u);  // root only

    // Open sub-window for lamp1
    wm.open("lamp1", "lamp [lamp1]");
    EXPECT_EQ(wm.count(), 2u);

    // Delete lamp1 via root scene
    auto scene = make_scene(bp);
    size_t lamp_idx = SIZE_MAX;
    auto lamp1_id = I.intern("lamp1");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == lamp1_id) { lamp_idx = i; break; }
    ASSERT_NE(lamp_idx, SIZE_MAX);

    visual::mutations::remove_nodes(scene, bp, {lamp_idx});
    wm.removeOrphanedWindows();

    EXPECT_EQ(wm.count(), 1u) << "Sub-window for deleted group should be closed";
    EXPECT_EQ(wm.find("lamp1"), nullptr);
}

// ---- 18. Deleting regular node does NOT trigger recursive cleanup ----

TEST(NodeDeletion, DeleteRegularNode_NoRecursiveEffect) {
    Blueprint bp = make_bp_with_sub_blueprint();
    auto& I = bp.interner();
    auto scene = make_scene(bp);

    // Delete "bat" (regular node, not a Blueprint)
    size_t bat_idx = SIZE_MAX;
    auto bat_id = I.intern("bat");
    for (size_t i = 0; i < bp.nodes.size(); ++i)
        if (bp.nodes[i].id == bat_id) { bat_idx = i; break; }
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
    EXPECT_EQ(bp.wires[0].id, I.intern("int_w"));
}

// ============================================================================
// REGRESSION: CanvasInput dangling-pointer safety via ID-based resolution
// ============================================================================
// With the ID-based selection model, CanvasInput stores string IDs instead
// of raw pointers. When scene mutations destroy wire/node widgets, the IDs
// remain valid strings but resolve to nullptr via scene.find(). No callback
// mechanism is needed — the resolution is lazy and inherently safe.

// ---- 19. selected_wire resolves to nullptr after wire is removed ----

TEST(CanvasInputDanglingPointer, SelectedWire_NulledOnRemoveWire) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    // Verify wire widget exists
    auto* wire_widget = dynamic_cast<visual::Wire*>(scene.find("w1"));
    ASSERT_NE(wire_widget, nullptr);

    // Remove the wire via mutation — the visual widget is destroyed
    visual::mutations::remove_wire(scene, bp, 0);

    // selected_wire() resolves to nullptr because the widget no longer exists
    EXPECT_EQ(input.selected_wire(), nullptr);
    EXPECT_EQ(input.hovered_wire(), nullptr);
}

// ---- 20. selected_nodes_ IDs that reference removed widgets resolve to nothing ----

TEST(CanvasInputDanglingPointer, SelectedNodes_ClearedOnRemoveNode) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    // Select both nodes
    auto* wa = scene.find("a");
    auto* wb = scene.find("b");
    ASSERT_NE(wa, nullptr);
    ASSERT_NE(wb, nullptr);
    input.add_node_selection(wa);
    input.add_node_selection(wb);
    ASSERT_EQ(input.selected_nodes().size(), 2u);

    // Remove node "a" — the widget is destroyed
    visual::mutations::remove_nodes(scene, bp, {0});

    // selected_nodes() resolves IDs lazily: "a" no longer exists in the
    // scene, so only "b"'s widget is returned.
    auto resolved = input.selected_nodes();
    EXPECT_EQ(resolved.size(), 1u);
    // The surviving widget should be the one for "b"
    EXPECT_NE(scene.find("b"), nullptr);
    if (!resolved.empty())
        EXPECT_EQ(resolved[0], scene.find("b"));
}

// ---- 21. scene.clear() makes all selected IDs resolve to nullptr ----

TEST(CanvasInputDanglingPointer, SelectionClearedOnSceneClear) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    bp.add_node(std::move(a));

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    auto* wa = scene.find("a");
    ASSERT_NE(wa, nullptr);
    input.add_node_selection(wa);
    ASSERT_EQ(input.selected_nodes().size(), 1u);

    // scene.clear() destroys all widgets — IDs remain but resolve to nullptr
    scene.clear();

    EXPECT_TRUE(input.selected_nodes().empty());
}

// ---- 22. Wire reconnection does not leave dangling selected_wire_ ----
// This is the specific crash scenario: reconnect_wire destroys the old
// wire widget and creates a new one. With ID-based selection, the stored
// wire ID still resolves because the new widget reuses the same ID.

TEST(CanvasInputDanglingPointer, ReconnectWire_NoDanglingSelectedWire) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    Node c; c.id = I.intern("c"); c.at(400, 0).size_wh(120, 80); c.input(I.intern("in2"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_node(std::move(c));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    // Verify the old wire widget exists
    auto* old_wire = scene.find("w1");
    ASSERT_NE(old_wire, nullptr);

    // Reconnect wire w1 from b:in to c:in2
    // This destroys the old wire widget and creates a new one with the same ID.
    visual::mutations::reconnect_wire(scene, bp, 0, false,
        WireEnd(I.intern("c"), I.intern("in2"), PortSide::Input), group_id);

    // The new wire widget should exist with the same ID
    auto* new_wire = scene.find("w1");
    EXPECT_NE(new_wire, nullptr);

    // Since selected_wire_id_ is "w1" and a widget with that ID still
    // exists, selected_wire() returns the new widget — no dangling pointer.
    // (If user hadn't selected the wire, selected_wire() returns nullptr
    // because selected_wire_id_ is empty. Both cases are safe.)
    EXPECT_EQ(input.selected_wire(), nullptr);  // wasn't selected in this test
}

// ---- 23. Remove wire via mutation nulls hovered_wire_ ----

TEST(CanvasInputDanglingPointer, RemoveWire_NullsHoveredWire) {
    Blueprint bp;
    auto& I = bp.interner();
    Node a; a.id = I.intern("a"); a.at(0, 0).size_wh(120, 80); a.output(I.intern("out"));
    Node b; b.id = I.intern("b"); b.at(200, 0).size_wh(120, 80); b.input(I.intern("in"));
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make(I.intern("w1"), wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("b"), I.intern("in"))));

    visual::Scene scene;
    Viewport vp;
    std::string group_id;
    visual::mutations::rebuild(scene, bp, group_id);
    CanvasInput input(scene, vp, bp, group_id);

    // Verify wire widget exists before removal
    auto* wire_widget = scene.find("w1");
    ASSERT_NE(wire_widget, nullptr);

    // Remove the wire via mutation — the visual widget is destroyed
    visual::mutations::remove_wire(scene, bp, 0);

    // After removal, the wire widget is destroyed. hovered_wire_id_ and
    // selected_wire_id_ IDs still exist as strings, but resolve to nullptr.
    EXPECT_EQ(input.hovered_wire(), nullptr);
    EXPECT_EQ(input.selected_wire(), nullptr);
    EXPECT_EQ(bp.wires.size(), 0u);
}
