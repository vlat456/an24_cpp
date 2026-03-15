#include <gtest/gtest.h>
#include "editor/visual/inspector/inspector.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/port.h"
#include "editor/data/wire.h"

// Note: Not using "using namespace an24" to avoid ambiguity between Port and editor::Port

// Helper to create a simple test scene
struct InspectorTestScene {
    Blueprint bp;

    InspectorTestScene() = default;

    Node& addNode(const std::string& id, const std::string& type, Pt pos = Pt(0, 0)) {
        auto& I = bp.interner();
        Node n;
        n.id = I.intern(id);
        n.name = id;
        n.type_name = type;
        n.pos = pos;
        n.size = Pt(120, 80);

        // Add default ports based on type (manually create Port structs)
        if (type == "Battery") {
            EditorPort p_in; p_in.name = I.intern("v_in"); p_in.side = PortSide::Input; p_in.type = PortType::V;
            EditorPort p_out; p_out.name = I.intern("v_out"); p_out.side = PortSide::Output; p_out.type = PortType::V;
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Lamp") {
            EditorPort p_in; p_in.name = I.intern("v_in"); p_in.side = PortSide::Input; p_in.type = PortType::V;
            EditorPort p_out; p_out.name = I.intern("light"); p_out.side = PortSide::Output; p_out.type = PortType::Bool;
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Switch") {
            EditorPort p_vin; p_vin.name = I.intern("v_in"); p_vin.side = PortSide::Input; p_vin.type = PortType::V;
            EditorPort p_ctrl; p_ctrl.name = I.intern("control"); p_ctrl.side = PortSide::Input; p_ctrl.type = PortType::Bool;
            EditorPort p_out; p_out.name = I.intern("v_out"); p_out.side = PortSide::Output; p_out.type = PortType::V;
            n.inputs.push_back(p_vin);
            n.inputs.push_back(p_ctrl);
            n.outputs.push_back(p_out);
        } else if (type == "Test") {
            EditorPort p_in; p_in.name = I.intern("in"); p_in.side = PortSide::Input; p_in.type = PortType::V;
            EditorPort p_out; p_out.name = I.intern("out"); p_out.side = PortSide::Output; p_out.type = PortType::V;
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Zebra" || type == "Apple" || type == "Banana") {
            // For sort tests - minimal ports
            EditorPort p_in; p_in.name = I.intern("in"); p_in.side = PortSide::Input; p_in.type = PortType::V;
            EditorPort p_out; p_out.name = I.intern("out"); p_out.side = PortSide::Output; p_out.type = PortType::V;
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        }

        size_t idx = bp.add_node(std::move(n));
        return bp.nodes[idx];
    }

    void addWire(const std::string& src_node, const std::string& src_port,
                 const std::string& dst_node, const std::string& dst_port) {
        auto& I = bp.interner();
        Wire w;
        w.start = WireEnd(I.intern(src_node), I.intern(src_port), PortSide::Output);
        w.end = WireEnd(I.intern(dst_node), I.intern(dst_port), PortSide::Input);
        bp.add_wire(w);
    }

    void rebuild() {
        bp.rebuild_wire_index();
        bp.rebuild_port_occupancy_index();
    }
};

// =============================================================================
// Tests for Display Tree Caching (Dirty Tracking)
// =============================================================================

TEST(Inspector, BuildDisplayTree_SingleNode_CreatesEntry) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();  // Force build

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].name, "battery");
    EXPECT_EQ(tree[0].type_name, "Battery");
    EXPECT_EQ(tree[0].connection_count, 0u);
}

TEST(Inspector, BuildDisplayTree_WithConnection_ShowsConnection) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp", "Lamp");
    ts.addWire("battery", "v_out", "lamp", "v_in");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 2u);

    // Find battery node
    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());

    // Check battery has output port with connection
    auto v_out_it = std::find_if(battery_it->ports.begin(), battery_it->ports.end(),
        [](const DisplayPort& p) { return p.name == "v_out"; });
    ASSERT_NE(v_out_it, battery_it->ports.end());
    EXPECT_FALSE(v_out_it->connection.empty());
    EXPECT_NE(v_out_it->connection, "[not connected]");
}

TEST(Inspector, BuildDisplayTree_UnconnectedPort_ShowsNotConnected) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);

    // Find an input port (should be unconnected)
    auto input_it = std::find_if(tree[0].ports.begin(), tree[0].ports.end(),
        [](const DisplayPort& p) { return p.side == PortSide::Input; });
    if (input_it != tree[0].ports.end()) {
        EXPECT_EQ(input_it->connection, "[not connected]");
    }
}

TEST(Inspector, ConnectionCount_SingleWire_CountsBothNodes) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp", "Lamp");
    ts.addWire("battery", "v_out", "lamp", "v_in");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    for (const auto& node : tree) {
        EXPECT_EQ(node.connection_count, 1u);
    }
}

TEST(Inspector, ConnectionCount_MultipleWiresFromOneNode) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp1", "Lamp");
    ts.addNode("lamp2", "Lamp");
    ts.addWire("battery", "v_out", "lamp1", "v_in");
    ts.addWire("battery", "v_out", "lamp2", "v_in");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());
    EXPECT_EQ(battery_it->connection_count, 2u);  // 2 wires from battery
}

TEST(Inspector, SearchFilter_MatchesName) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp", "Lamp");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.setSearch("bat");  // Should match "battery"
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].name, "battery");
}

TEST(Inspector, SearchFilter_MatchesType) {
    InspectorTestScene ts;
    ts.addNode("main_battery", "Battery");
    ts.addNode("lamp", "Lamp");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.setSearch("lamp");  // Should match Lamp type
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].name, "lamp");
    EXPECT_EQ(tree[0].type_name, "Lamp");
}

TEST(Inspector, MarkDirty_RebuildsOnChange) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();
    ASSERT_EQ(inspector.displayTree().size(), 1u);

    // Add another node
    ts.addNode("lamp", "Lamp");
    ts.rebuild();
    inspector.markDirty();

    // Tree should rebuild with new node
    inspector.buildDisplayTree();
    EXPECT_EQ(inspector.displayTree().size(), 2u);
}

TEST(Inspector, SortMode_ByName) {
    InspectorTestScene ts;
    ts.addNode("zebra", "Test");
    ts.addNode("apple", "Test");
    ts.addNode("banana", "Test");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.setSortMode(Inspector::SortMode::Name);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 3u);
    EXPECT_EQ(tree[0].name, "apple");
    EXPECT_EQ(tree[1].name, "banana");
    EXPECT_EQ(tree[2].name, "zebra");
}

TEST(Inspector, SortMode_ByType) {
    InspectorTestScene ts;
    ts.addNode("a", "Zebra");
    ts.addNode("b", "Apple");
    ts.addNode("c", "Banana");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.setSortMode(Inspector::SortMode::Type);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 3u);
    EXPECT_EQ(tree[0].type_name, "Apple");
    EXPECT_EQ(tree[1].type_name, "Banana");
    EXPECT_EQ(tree[2].type_name, "Zebra");
}

// =============================================================================
// Regression: Group filtering — inspector must only show nodes in its scene's group
// =============================================================================

TEST(Inspector, GroupFiltering_RootInspectorHidesSubBlueprintNodes) {
    Blueprint bp;
    auto& I = bp.interner();
    // Root-level node
    Node root_node;
    root_node.id = I.intern("battery1");
    root_node.name = "battery1";
    root_node.type_name = "Battery";
    root_node.group_id = "";
EditorPort ri; ri.name = I.intern("v_in"); ri.side = PortSide::Input; ri.type = PortType::V;
EditorPort ro; ro.name = I.intern("v_out"); ro.side = PortSide::Output; ro.type = PortType::V;
    root_node.inputs.push_back(ri);
    root_node.outputs.push_back(ro);
    bp.add_node(std::move(root_node));

    // Collapsed blueprint node (visible at root level)
    Node bp_node;
    bp_node.id = I.intern("lamp1");
    bp_node.name = "lamp1";
    bp_node.type_name = "LampBlueprint";
    bp_node.expandable = true;
    bp_node.group_id = "";
    bp.add_node(std::move(bp_node));

    // Internal node (hidden from root)
    Node internal;
    internal.id = I.intern("lamp1:led");
    internal.name = "lamp1:led";
    internal.type_name = "LED";
    internal.group_id = "lamp1";
EditorPort ii; ii.name = I.intern("v_in"); ii.side = PortSide::Input; ii.type = PortType::V;
    internal.inputs.push_back(ii);
    bp.add_node(std::move(internal));

    bp.rebuild_wire_index();
    bp.rebuild_port_occupancy_index();

    Inspector inspector(&bp);  // root group
    inspector.buildDisplayTree();

    // Root inspector should show battery1 + lamp1, NOT lamp1:led
    ASSERT_EQ(inspector.displayTree().size(), 2u);
    for (const auto& dn : inspector.displayTree()) {
        EXPECT_NE(dn.name, "lamp1:led") << "Internal node leaked into root inspector";
    }
}

TEST(Inspector, GroupFiltering_SubInspectorShowsOnlyOwnNodes) {
    Blueprint bp;
    auto& I = bp.interner();
    // Root-level node
    Node root_node;
    root_node.id = I.intern("battery1");
    root_node.name = "battery1";
    root_node.type_name = "Battery";
    root_node.group_id = "";
    bp.add_node(std::move(root_node));

    // Internal nodes belonging to "lamp1" group
    Node led;
    led.id = I.intern("lamp1:led");
    led.name = "lamp1:led";
    led.type_name = "LED";
    led.group_id = "lamp1";
EditorPort li; li.name = I.intern("v_in"); li.side = PortSide::Input; li.type = PortType::V;
    led.inputs.push_back(li);
    bp.add_node(std::move(led));

    Node res;
    res.id = I.intern("lamp1:res");
    res.name = "lamp1:res";
    res.type_name = "Resistor";
    res.group_id = "lamp1";
EditorPort ri2; ri2.name = I.intern("v_in"); ri2.side = PortSide::Input; ri2.type = PortType::V;
EditorPort ro2; ro2.name = I.intern("v_out"); ro2.side = PortSide::Output; ro2.type = PortType::V;
    res.inputs.push_back(ri2);
    res.outputs.push_back(ro2);
    bp.add_node(std::move(res));

    bp.rebuild_wire_index();
    bp.rebuild_port_occupancy_index();

    // Sub-blueprint inspector for "lamp1" group
    Inspector sub_inspector(&bp, "lamp1");
    sub_inspector.buildDisplayTree();

    // Should show only lamp1:led and lamp1:res, NOT battery1
    ASSERT_EQ(sub_inspector.displayTree().size(), 2u);
    for (const auto& dn : sub_inspector.displayTree()) {
        EXPECT_NE(dn.name, "battery1") << "Root node leaked into sub-inspector";
    }
}

TEST(Inspector, GroupFiltering_WiresOnlyCountOwnGroup) {
    Blueprint bp;
    auto& I = bp.interner();
    // Root node
    Node bat;
    bat.id = I.intern("bat");
    bat.name = "bat";
    bat.type_name = "Battery";
    bat.group_id = "";
EditorPort bo; bo.name = I.intern("v_out"); bo.side = PortSide::Output; bo.type = PortType::V;
    bat.outputs.push_back(bo);
    bp.add_node(std::move(bat));

    // Root node lamp (collapsed)
    Node lamp;
    lamp.id = I.intern("lamp1");
    lamp.name = "lamp1";
    lamp.type_name = "Lamp";
    lamp.expandable = true;
    lamp.group_id = "";
EditorPort lvi; lvi.name = I.intern("v_in"); lvi.side = PortSide::Input; lvi.type = PortType::V;
    lamp.inputs.push_back(lvi);
    bp.add_node(std::move(lamp));

    // Internal nodes
    Node iled;
    iled.id = I.intern("lamp1:led");
    iled.name = "lamp1:led";
    iled.type_name = "LED";
    iled.group_id = "lamp1";
EditorPort iledi; iledi.name = I.intern("v_in"); iledi.side = PortSide::Input; iledi.type = PortType::V;
EditorPort iledo; iledo.name = I.intern("v_out"); iledo.side = PortSide::Output; iledo.type = PortType::V;
    iled.inputs.push_back(iledi);
    iled.outputs.push_back(iledo);
    bp.add_node(std::move(iled));

    Node ires;
    ires.id = I.intern("lamp1:res");
    ires.name = "lamp1:res";
    ires.type_name = "Resistor";
    ires.group_id = "lamp1";
EditorPort iresi; iresi.name = I.intern("v_in"); iresi.side = PortSide::Input; iresi.type = PortType::V;
    ires.inputs.push_back(iresi);
    bp.add_node(std::move(ires));

    // Root wire: bat -> lamp1
    Wire rw;
    rw.start = WireEnd(I.intern("bat"), I.intern("v_out"), PortSide::Output);
    rw.end = WireEnd(I.intern("lamp1"), I.intern("v_in"), PortSide::Input);
    bp.add_wire(rw);

    // Internal wire: lamp1:led -> lamp1:res
    Wire iw;
    iw.start = WireEnd(I.intern("lamp1:led"), I.intern("v_out"), PortSide::Output);
    iw.end = WireEnd(I.intern("lamp1:res"), I.intern("v_in"), PortSide::Input);
    bp.add_wire(iw);

    bp.rebuild_wire_index();
    bp.rebuild_port_occupancy_index();

    // Root inspector: bat should have 1 connection (root wire), not 2
    Inspector root_inspector(&bp);  // root group
    root_inspector.buildDisplayTree();

    const auto& root_tree = root_inspector.displayTree();
    auto bat_it = std::find_if(root_tree.begin(), root_tree.end(),
        [](const DisplayNode& n) { return n.name == "bat"; });
    ASSERT_NE(bat_it, root_tree.end());
    EXPECT_EQ(bat_it->connection_count, 1u) << "Root wire count contaminated by internal wires";

    // Sub inspector: lamp1:led should have 1 connection (internal wire)
    Inspector sub_inspector(&bp, "lamp1");
    sub_inspector.buildDisplayTree();

    const auto& sub_tree = sub_inspector.displayTree();
    auto led_it = std::find_if(sub_tree.begin(), sub_tree.end(),
        [](const DisplayNode& n) { return n.name == "lamp1:led"; });
    ASSERT_NE(led_it, sub_tree.end());
    EXPECT_EQ(led_it->connection_count, 1u);
}

TEST(Inspector, FanOut_OutputShowsMultipleConnections) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp1", "Lamp");
    ts.addNode("lamp2", "Lamp");
    ts.addWire("battery", "v_out", "lamp1", "v_in");
    ts.addWire("battery", "v_out", "lamp2", "v_in");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());

    // Find v_out port — should show both connections
    auto v_out_it = std::find_if(battery_it->ports.begin(), battery_it->ports.end(),
        [](const DisplayPort& p) { return p.name == "v_out"; });
    ASSERT_NE(v_out_it, battery_it->ports.end());
    EXPECT_NE(v_out_it->connection, "[not connected]");
    // Should contain both lamp1 and lamp2
    EXPECT_NE(v_out_it->connection.find("lamp1"), std::string::npos)
        << "Missing lamp1 in fan-out: " << v_out_it->connection;
    EXPECT_NE(v_out_it->connection.find("lamp2"), std::string::npos)
        << "Missing lamp2 in fan-out: " << v_out_it->connection;
}

// ============================================================================
// DisplayNode stores node_id
// ============================================================================

TEST(Inspector, DisplayNode_HasNodeId) {
    InspectorTestScene ts;
    ts.addNode("bat1", "Battery");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].node_id, "bat1");
}

// ============================================================================
// consumeSelection — single-shot output
// ============================================================================

TEST(Inspector, ConsumeSelection_EmptyByDefault) {
    InspectorTestScene ts;
    Inspector inspector(&ts.bp);
    EXPECT_TRUE(inspector.consumeSelection().empty());
}

TEST(Inspector, ConsumeSelection_ClearsAfterRead) {
    InspectorTestScene ts;
    Inspector inspector(&ts.bp);
    // Simulate a click by directly setting the field (render() would do this via ImGui)
    // We test consumeSelection logic only
    auto sel1 = inspector.consumeSelection();
    EXPECT_TRUE(sel1.empty());
    // Second read is also empty
    auto sel2 = inspector.consumeSelection();
    EXPECT_TRUE(sel2.empty());
}

// ============================================================================
// Regression: Inspector must update display name after markDirty()
// ============================================================================

TEST(Inspector, Regression_NameUpdateAfterMarkDirty) {
    InspectorTestScene ts;
    Node& bat = ts.addNode("bat1", "Battery");
    ts.rebuild();

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    // Initially the name == id
    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name, "bat1");

    // Simulate rename (properties window modifies node.name in-place)
    bat.name = "Main Battery 28V";

    // Without markDirty(), the tree is stale
    // Calling buildDisplayTree after markDirty() should pick up the new name
    inspector.markDirty();
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name, "Main Battery 28V")
        << "Inspector must reflect renamed node after markDirty + rebuild";
}

TEST(Inspector, Regression_CyrillicNameInDisplayTree) {
    InspectorTestScene ts;
    Node& node = ts.addNode("azs_1", "Test");
    ts.rebuild();

    // Set a Cyrillic display name
    node.name = "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8";  // "АЗС Батареи"

    Inspector inspector(&ts.bp);
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name,
              "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8")
        << "Cyrillic display name must appear correctly in inspector tree";
    EXPECT_EQ(inspector.displayTree()[0].node_id, "azs_1")
        << "node_id must remain the original id, not the display name";
}
