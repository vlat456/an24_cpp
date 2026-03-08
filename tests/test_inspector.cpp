#include <gtest/gtest.h>
#include "editor/visual/inspector/inspector.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/port.h"
#include "editor/data/wire.h"
#include "editor/visual/scene/scene.h"

// Note: Not using "using namespace an24" to avoid ambiguity between an24::Port and editor::Port

// Helper to create a simple test scene
struct InspectorTestScene {
    Blueprint bp;
    VisualScene scene;

    InspectorTestScene() : scene(bp, "") {}

    Node& addNode(const std::string& id, const std::string& type, Pt pos = Pt(0, 0)) {
        Node n;
        n.id = id;
        n.name = id;
        n.type_name = type;
        n.kind = NodeKind::Node;
        n.pos = pos;
        n.size = Pt(120, 80);

        // Add default ports based on type (manually create Port structs)
        if (type == "Battery") {
            Port p_in{"v_in", PortSide::Input, an24::PortType::V};
            Port p_out{"v_out", PortSide::Output, an24::PortType::V};
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Lamp") {
            Port p_in{"v_in", PortSide::Input, an24::PortType::V};
            Port p_out{"light", PortSide::Output, an24::PortType::Bool};
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Switch") {
            Port p_vin{"v_in", PortSide::Input, an24::PortType::V};
            Port p_ctrl{"control", PortSide::Input, an24::PortType::Bool};
            Port p_out{"v_out", PortSide::Output, an24::PortType::V};
            n.inputs.push_back(p_vin);
            n.inputs.push_back(p_ctrl);
            n.outputs.push_back(p_out);
        } else if (type == "Test") {
            Port p_in{"in", PortSide::Input, an24::PortType::V};
            Port p_out{"out", PortSide::Output, an24::PortType::V};
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        } else if (type == "Zebra" || type == "Apple" || type == "Banana") {
            // For sort tests - minimal ports
            Port p_in{"in", PortSide::Input, an24::PortType::V};
            Port p_out{"out", PortSide::Output, an24::PortType::V};
            n.inputs.push_back(p_in);
            n.outputs.push_back(p_out);
        }

        size_t idx = bp.add_node(std::move(n));
        return bp.nodes[idx];
    }

    void addWire(const std::string& src_node, const std::string& src_port,
                 const std::string& dst_node, const std::string& dst_port) {
        Wire w;
        w.start = WireEnd(src_node.c_str(), src_port.c_str(), PortSide::Output);
        w.end = WireEnd(dst_node.c_str(), dst_port.c_str(), PortSide::Input);
        bp.add_wire(w);
    }

    void rebuild() {
        bp.rebuild_wire_index();
    }
};

// =============================================================================
// Tests for Display Tree Caching (Dirty Tracking)
// =============================================================================

TEST(Inspector, BuildDisplayTree_SingleNode_CreatesEntry) {
    InspectorTestScene ts;
    ts.addNode("battery", "Battery");
    ts.rebuild();

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
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

    Inspector inspector(ts.scene);
    inspector.setSortMode(Inspector::SortMode::Type);
    inspector.buildDisplayTree();

    const auto& tree = inspector.displayTree();
    ASSERT_EQ(tree.size(), 3u);
    EXPECT_EQ(tree[0].type_name, "Apple");
    EXPECT_EQ(tree[1].type_name, "Banana");
    EXPECT_EQ(tree[2].type_name, "Zebra");
}
