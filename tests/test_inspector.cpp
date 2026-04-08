#include <gtest/gtest.h>
#include "editor/visual/inspector/inspector.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

// Helper to create a simple test scene using bp2::Blueprint
struct InspectorTestScene {
    ui::StringInterner interner;
    bp2::PathArena arena;
    bp2::Blueprint bp;

    InspectorTestScene() : arena(interner) {}

    /// Add a node to the blueprint with optional group_id.
    bp2::Blueprint::Node& addNode(const std::string& id,
                                   const std::string& type,
                                   const std::string& group_id = "") {
        bp2::Blueprint::Node n;
         n.semantic.id = interner.intern(id);
         n.semantic.type = interner.intern(type);
         n.view.name = id;
         n.layout.group_id = group_id;

        if (type == "Battery") {
            n.view.inputs.push_back(bp2::NodePort(interner.intern("v_in"), bp2::PortSide::Input,  PortType::V));
            n.view.outputs.push_back(bp2::NodePort(interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        } else if (type == "Lamp") {
            n.view.inputs.push_back(bp2::NodePort(interner.intern("v_in"), bp2::PortSide::Input,  PortType::V));
            n.view.outputs.push_back(bp2::NodePort(interner.intern("light"), bp2::PortSide::Output, PortType::Bool));
        } else if (type == "Switch") {
            n.view.inputs.push_back(bp2::NodePort(interner.intern("v_in"), bp2::PortSide::Input, PortType::V));
            n.view.inputs.push_back(bp2::NodePort(interner.intern("control"), bp2::PortSide::Input, PortType::Bool));
            n.view.outputs.push_back(bp2::NodePort(interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        } else {
            // Generic: in + out
            n.view.inputs.push_back(bp2::NodePort(interner.intern("in"), bp2::PortSide::Input,  PortType::V));
            n.view.outputs.push_back(bp2::NodePort(interner.intern("out"), bp2::PortSide::Output, PortType::V));
        }

         bp = bp.with_node(n);
         // Return a reference to the node just added
         return const_cast<bp2::Blueprint::Node&>(*bp.find_node(n.semantic.id));
    }

    /// Add a minimal node with explicit ports (for group-filtering tests).
    void addNodeRaw(bp2::Blueprint::Node n) {
        bp = bp.with_node(std::move(n));
    }

    /// Build a port Path: root → node(node_id) → port(port_name)
    bp2::Path makePortPath(const std::string& node_id, const std::string& port_name) {
        bp2::Path node_path = arena.make_node(arena.root(), interner.intern(node_id));
        return arena.make_port(node_path, interner.intern(port_name));
    }

    void addWire(const std::string& src_node, const std::string& src_port,
                 const std::string& dst_node, const std::string& dst_port) {
         static int wire_counter = 0;
         bp2::Blueprint::Wire w;
         w.id = interner.intern("wire_" + std::to_string(wire_counter++));
        w.source = makePortPath(src_node, src_port);
        w.target = makePortPath(dst_node, dst_port);
        bp = bp.with_wire(w);
    }
};

// =============================================================================
// Tests for Display Tree Caching (Dirty Tracking)
// =============================================================================

TEST(Inspector, BuildDisplayTree_SingleNode_CreatesEntry) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

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

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 2u);

    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());

    auto v_out_it = std::find_if(battery_it->ports.begin(), battery_it->ports.end(),
        [](const DisplayPort& p) { return p.name == "v_out"; });
    ASSERT_NE(v_out_it, battery_it->ports.end());
    EXPECT_FALSE(v_out_it->connection.empty());
    EXPECT_NE(v_out_it->connection, "[not connected]");
}

TEST(Inspector, BuildDisplayTree_UnconnectedPort_ShowsNotConnected) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);

    auto input_it = std::find_if(tree[0].ports.begin(), tree[0].ports.end(),
        [](const DisplayPort& p) { return p.side == bp2::PortSide::Input; });
    if (input_it != tree[0].ports.end()) {
        EXPECT_EQ(input_it->connection, "[not connected]");
    }
}

TEST(Inspector, ConnectionCount_SingleWire_CountsBothNodes) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp", "Lamp");
    ts.addWire("battery", "v_out", "lamp", "v_in");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
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

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());
    EXPECT_EQ(battery_it->connection_count, 2u);
}

TEST(Inspector, SearchFilter_MatchesName) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.addNode("lamp", "Lamp");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.setSearch("bat");
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].name, "battery");
}

TEST(Inspector, SearchFilter_MatchesType) {
    InspectorTestScene ts;
    ts.addNode("main_battery", "Battery");
    ts.addNode("lamp", "Lamp");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.setSearch("lamp");
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].name, "lamp");
    EXPECT_EQ(tree[0].type_name, "Lamp");
}

TEST(Inspector, MarkDirty_RebuildsOnChange) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();
    ASSERT_EQ(inspector.displayTree().size(), 1u);

    // Add another node and update the blueprint reference
    ts.addNode("lamp", "Lamp");
    inspector.setBlueprint(ts.bp, ts.arena, ts.interner);
    inspector.markDirty();

    inspector.buildDisplayTree();
    EXPECT_EQ(inspector.displayTree().size(), 2u);
}

TEST(Inspector, SortMode_ByName) {
    InspectorTestScene ts;
    ts.addNode("zebra", "Test");
    ts.addNode("apple", "Test");
    ts.addNode("banana", "Test");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
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

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
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
    InspectorTestScene ts;

    {
        bp2::Blueprint::Node root_node;
        root_node.semantic.id = ts.interner.intern("battery1");
        root_node.semantic.type = ts.interner.intern("Battery");
        root_node.view.name = "battery1";
        root_node.layout.group_id = "";
        root_node.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input,  PortType::V));
        root_node.view.outputs.push_back(bp2::NodePort(ts.interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        ts.addNodeRaw(std::move(root_node));
    }
    {
        bp2::Blueprint::Node bp_node;
        bp_node.semantic.id = ts.interner.intern("lamp1");
        bp_node.semantic.type = ts.interner.intern("LampBlueprint");
        bp_node.view.name = "lamp1";
        bp_node.view.expandable = true;
        bp_node.layout.group_id = "";
        ts.addNodeRaw(std::move(bp_node));
    }
    {
        bp2::Blueprint::Node internal;
        internal.semantic.id = ts.interner.intern("lamp1:led");
        internal.semantic.type = ts.interner.intern("LED");
        internal.view.name = "lamp1:led";
        internal.layout.group_id = "lamp1";
        internal.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input, PortType::V));
        ts.addNodeRaw(std::move(internal));
    }

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner, "");
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 2u);
    for (const auto& dn : inspector.displayTree()) {
        EXPECT_NE(dn.name, "lamp1:led") << "Internal node leaked into root inspector";
    }
}

TEST(Inspector, GroupFiltering_SubInspectorShowsOnlyOwnNodes) {
    InspectorTestScene ts;

    {
        bp2::Blueprint::Node root_node;
        root_node.semantic.id = ts.interner.intern("battery1");
        root_node.semantic.type = ts.interner.intern("Battery");
        root_node.view.name = "battery1";
        root_node.layout.group_id = "";
        ts.addNodeRaw(std::move(root_node));
    }
    {
        bp2::Blueprint::Node led;
        led.semantic.id = ts.interner.intern("lamp1:led");
        led.semantic.type = ts.interner.intern("LED");
        led.view.name = "lamp1:led";
        led.layout.group_id = "lamp1";
        led.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input, PortType::V));
        ts.addNodeRaw(std::move(led));
    }
    {
        bp2::Blueprint::Node res;
        res.semantic.id = ts.interner.intern("lamp1:res");
        res.semantic.type = ts.interner.intern("Resistor");
        res.view.name = "lamp1:res";
        res.layout.group_id = "lamp1";
        res.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input,  PortType::V));
        res.view.outputs.push_back(bp2::NodePort(ts.interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        ts.addNodeRaw(std::move(res));
    }

    Inspector sub_inspector(&ts.bp, &ts.arena, &ts.interner, "lamp1");
    sub_inspector.buildDisplayTree();

    ASSERT_EQ(sub_inspector.displayTree().size(), 2u);
    for (const auto& dn : sub_inspector.displayTree()) {
        EXPECT_NE(dn.name, "battery1") << "Root node leaked into sub-inspector";
    }
}

TEST(Inspector, GroupFiltering_WiresOnlyCountOwnGroup) {
    InspectorTestScene ts;

    // Root bat
    {
        bp2::Blueprint::Node bat;
        bat.semantic.id = ts.interner.intern("bat");
        bat.semantic.type = ts.interner.intern("Battery");
        bat.view.name = "bat";
        bat.layout.group_id = "";
        bat.view.outputs.push_back(bp2::NodePort(ts.interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        ts.addNodeRaw(std::move(bat));
    }
    // Root lamp1 (collapsed)
    {
        bp2::Blueprint::Node lamp;
        lamp.semantic.id = ts.interner.intern("lamp1");
        lamp.semantic.type = ts.interner.intern("Lamp");
         lamp.view.name = "lamp1";
        lamp.view.expandable = true;
        lamp.layout.group_id = "";
        lamp.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input, PortType::V));
        ts.addNodeRaw(std::move(lamp));
    }
    // Internal nodes
    {
        bp2::Blueprint::Node iled;
        iled.semantic.id = ts.interner.intern("lamp1:led");
        iled.semantic.type = ts.interner.intern("LED");
        iled.view.name = "lamp1:led";
        iled.layout.group_id = "lamp1";
        iled.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input,  PortType::V));
        iled.view.outputs.push_back(bp2::NodePort(ts.interner.intern("v_out"), bp2::PortSide::Output, PortType::V));
        ts.addNodeRaw(std::move(iled));
    }
    {
        bp2::Blueprint::Node ires;
        ires.semantic.id = ts.interner.intern("lamp1:res");
        ires.semantic.type = ts.interner.intern("Resistor");
        ires.view.name = "lamp1:res";
        ires.layout.group_id = "lamp1";
        ires.view.inputs.push_back(bp2::NodePort(ts.interner.intern("v_in"), bp2::PortSide::Input, PortType::V));
        ts.addNodeRaw(std::move(ires));
    }

    // Root wire: bat:v_out -> lamp1:v_in
    ts.addWire("bat", "v_out", "lamp1", "v_in");
    // Internal wire: lamp1:led:v_out -> lamp1:res:v_in
    ts.addWire("lamp1:led", "v_out", "lamp1:res", "v_in");

    // Root inspector
    Inspector root_inspector(&ts.bp, &ts.arena, &ts.interner, "");
    root_inspector.buildDisplayTree();

    const auto& root_tree = root_inspector.displayTree();
    auto bat_it = std::find_if(root_tree.begin(), root_tree.end(),
        [](const DisplayNode& n) { return n.name == "bat"; });
    ASSERT_NE(bat_it, root_tree.end());
    EXPECT_EQ(bat_it->connection_count, 1u) << "Root wire count contaminated by internal wires";

    // Sub inspector
    Inspector sub_inspector(&ts.bp, &ts.arena, &ts.interner, "lamp1");
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

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    auto battery_it = std::find_if(tree.begin(), tree.end(),
        [](const DisplayNode& n) { return n.name == "battery"; });
    ASSERT_NE(battery_it, tree.end());

    auto v_out_it = std::find_if(battery_it->ports.begin(), battery_it->ports.end(),
        [](const DisplayPort& p) { return p.name == "v_out"; });
    ASSERT_NE(v_out_it, battery_it->ports.end());
    EXPECT_NE(v_out_it->connection, "[not connected]");
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

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
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
    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    EXPECT_TRUE(inspector.consumeSelection().empty());
}

TEST(Inspector, ConsumeSelection_ClearsAfterRead) {
    InspectorTestScene ts;
    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    auto sel1 = inspector.consumeSelection();
    EXPECT_TRUE(sel1.empty());
    auto sel2 = inspector.consumeSelection();
    EXPECT_TRUE(sel2.empty());
}

// ============================================================================
// Regression: Inspector must update display name after markDirty()
// ============================================================================

TEST(Inspector, Regression_NameUpdateAfterMarkDirty) {
    InspectorTestScene ts;
    ts.addNode("bat1", "Battery");

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name, "bat1");

    // Mutate the blueprint: replace the node with a renamed version
    bp2::Blueprint::Node updated = *ts.bp.find_node(ts.interner.intern("bat1"));
    updated.view.name = "Main Battery 28V";
    ts.bp = ts.bp.without_node(ts.interner.intern("bat1")).with_node(updated);

    inspector.setBlueprint(ts.bp, ts.arena, ts.interner);
    inspector.markDirty();
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name, "Main Battery 28V")
        << "Inspector must reflect renamed node after markDirty + rebuild";
}

TEST(Inspector, Regression_CyrillicNameInDisplayTree) {
    InspectorTestScene ts;
    ts.addNode("azs_1", "Test");

    // Set a Cyrillic display name via bp mutation
    bp2::Blueprint::Node updated = *ts.bp.find_node(ts.interner.intern("azs_1"));
    updated.view.name = "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8";  // "АЗС Батареи"
    ts.bp = ts.bp.without_node(ts.interner.intern("azs_1")).with_node(updated);

    Inspector inspector(&ts.bp, &ts.arena, &ts.interner);
    inspector.buildDisplayTree();

    ASSERT_EQ(inspector.displayTree().size(), 1u);
    EXPECT_EQ(inspector.displayTree()[0].name,
              "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8")
        << "Cyrillic display name must appear correctly in inspector tree";
    EXPECT_EQ(inspector.displayTree()[0].node_id, "azs_1")
        << "node_id must remain the original id, not the display name";
}
