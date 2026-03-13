#include <gtest/gtest.h>
#include "visual/scene_mutations.h"
#include "visual/scene.h"
#include "visual/node/node_factory.h"
#include "visual/wire/wire.h"
#include "visual/wire/wire_end.h"
#include "visual/wire/routing_point.h"
#include "visual/node/bus_node_widget.h"
#include "data/blueprint.h"
#include "data/node.h"
#include "data/wire.h"

// ============================================================================
// rebuild
// ============================================================================

TEST(SceneMutations, RebuildCreatesNodeWidgets) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.input("v_in", PortType::V);
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    EXPECT_EQ(scene.roots().size(), 2u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_NE(scene.find("lamp1"), nullptr);
}

TEST(SceneMutations, RebuildFiltersGroupId) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "inner1"; n2.name = "Inner"; n2.type_name = "Lamp";
    n2.group_id = "group_A";
    bp.add_node(std::move(n2));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // Only the root-level node should appear
    EXPECT_EQ(scene.roots().size(), 1u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_EQ(scene.find("inner1"), nullptr);
}

TEST(SceneMutations, RebuildCreatesWireWidgets) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 2 nodes + 1 wire = 3 roots
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

TEST(SceneMutations, RebuildClearsExistingScene) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    bp.add_node(std::move(n1));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(scene.roots().size(), 1u);

    // Rebuild again — should clear first
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(scene.roots().size(), 1u);
}

// ============================================================================
// add_node
// ============================================================================

TEST(SceneMutations, AddNodeCreatesWidgetAndBlueprintEntry) {
    Blueprint bp;
    visual::Scene scene;

    Node n;
    n.id = "bat1"; n.name = "Battery"; n.type_name = "Battery";

    size_t idx = visual::mutations::add_node(scene, bp, std::move(n), "");

    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].group_id, "");
    EXPECT_NE(scene.find("bat1"), nullptr);
}

// ============================================================================
// remove_nodes
// ============================================================================

TEST(SceneMutations, RemoveNodesCleansUpWidgetsAndBlueprint) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(scene.roots().size(), 3u); // 2 nodes + 1 wire

    // Remove bat1 (index 0) — should also remove connected wire
    visual::mutations::remove_nodes(scene, bp, {0});

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, "lamp1");
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(scene.find("bat1"), nullptr);
    EXPECT_EQ(scene.find("wire_0"), nullptr);
    EXPECT_NE(scene.find("lamp1"), nullptr);
}

// ============================================================================
// add_wire
// ============================================================================

TEST(SceneMutations, AddWireCreatesWidgetAndBlueprintEntry) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(scene.roots().size(), 2u);

    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("lamp1", "v_in"));
    bool ok = visual::mutations::add_wire(scene, bp, std::move(w), "");

    EXPECT_TRUE(ok);
    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

TEST(SceneMutations, AddWireRejectsWrongGroup) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "other_group";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    visual::Scene scene;
    // Build scene for root group (only lamp1 visible)
    visual::mutations::rebuild(scene, bp, "");

    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("lamp1", "v_in"));
    bool ok = visual::mutations::add_wire(scene, bp, std::move(w), "");

    EXPECT_FALSE(ok);
    EXPECT_EQ(bp.wires.size(), 0u);
}

// ============================================================================
// remove_wire
// ============================================================================

TEST(SceneMutations, RemoveWireRemovesWidgetAndBlueprintEntry) {
    Blueprint bp;
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_NE(scene.find("wire_0"), nullptr);

    visual::mutations::remove_wire(scene, bp, 0);

    EXPECT_EQ(bp.wires.size(), 0u);
    // After flush, wire widget should be gone
    scene.flushRemovals();
    EXPECT_EQ(scene.find("wire_0"), nullptr);
}

// ============================================================================
// next_wire_id
// ============================================================================

TEST(SceneMutations, NextWireIdIsMonotonic) {
    Blueprint bp;
    bp.next_wire_id = 5;

    EXPECT_EQ(visual::mutations::next_wire_id(bp), "wire_5");
    EXPECT_EQ(visual::mutations::next_wire_id(bp), "wire_6");
    EXPECT_EQ(visual::mutations::next_wire_id(bp), "wire_7");
    EXPECT_EQ(bp.next_wire_id, 8);
}

// ============================================================================
// Bus node wire operations
// ============================================================================

TEST(SceneMutations, RebuildWithBusNodeCreatesAliasPortWires) {
    Blueprint bp;

    // Bus node with "v" port (render_hint="bus")
    Node bus;
    bus.id = "bus1"; bus.name = "Main Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    // Regular node
    Node n2;
    n2.id = "bat1"; n2.name = "Battery"; n2.type_name = "Battery";
    n2.group_id = "";
    n2.output("v_out", PortType::V);
    bp.add_node(std::move(n2));

    // Wire connecting bat1:v_out -> bus1:v
    auto w = Wire::make("wire_0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    bp.add_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 2 nodes + 1 wire
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("bus1"), nullptr);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

// ============================================================================
// REGRESSION: Multiple wires on bus survive individual wire removal
// ============================================================================
// Before the fix, removing one wire from a bus caused cascading destruction:
// rebuildPorts() destroyed all Port children, triggering WireEnd destructors,
// which notified Wire to remove itself from the Scene. This made all wires
// on the bus vanish when any single wire was modified.

TEST(SceneMutations, RemoveOneWireFromBusPreservesOthers) {
    Blueprint bp;

    // Bus node
    Node bus;
    bus.id = "bus1"; bus.name = "Main Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    // Three regular nodes
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "load1"; n2.name = "Load1"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Node n3;
    n3.id = "load2"; n3.name = "Load2"; n3.type_name = "Lamp";
    n3.group_id = "";
    n3.input("v_in", PortType::V);
    bp.add_node(std::move(n3));

    // Three wires all touching bus1:v
    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("load1", "v_in")));
    bp.add_wire(Wire::make("w2",
        wire_output("bus1", "v"),
        wire_input("load2", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 4 nodes + 3 wires = 7
    EXPECT_EQ(scene.roots().size(), 7u);
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);
    EXPECT_NE(scene.find("w2"), nullptr);

    // Remove wire w0 (index 0)
    visual::mutations::remove_wire(scene, bp, 0);
    scene.flushRemovals();

    // w0 should be gone, but w1 and w2 must survive
    EXPECT_EQ(bp.wires.size(), 2u);
    EXPECT_EQ(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);
    EXPECT_NE(scene.find("w2"), nullptr);
}

// REGRESSION: Rebuild with multiple bus wires creates all wire widgets
TEST(SceneMutations, RebuildMultipleBusWires) {
    Blueprint bp;

    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    Node n1;
    n1.id = "bat1"; n1.name = "Bat"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 3 nodes + 2 wires = 5
    EXPECT_EQ(scene.roots().size(), 5u);
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);

    // Bus should have alias ports for both wires
    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus1"));
    ASSERT_NE(bus_widget, nullptr);
    EXPECT_NE(bus_widget->port("w0"), nullptr);
    EXPECT_NE(bus_widget->port("w1"), nullptr);
}

// ============================================================================
// REGRESSION: Scene rebuild preserves custom colors from data layer
// ============================================================================
// After "Apply" in the color picker, Node.color is set. A subsequent
// rebuild must propagate the color to the new visual widget.

TEST(SceneMutations, RebuildPreservesNodeColor) {
    Blueprint bp;

    Node n;
    n.id = "bat1"; n.name = "Battery"; n.type_name = "Battery";
    n.group_id = "";
    n.color = NodeColor{0.8f, 0.2f, 0.1f, 1.0f};
    bp.add_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildPreservesBusNodeColor) {
    Blueprint bp;

    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.color = NodeColor{0.1f, 0.5f, 0.9f, 1.0f};
    bp.add_node(std::move(bus));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    auto* w = scene.find("bus1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildNoColorWhenNodeHasNoColor) {
    Blueprint bp;

    Node n;
    n.id = "bat1"; n.name = "Battery"; n.type_name = "Battery";
    n.group_id = "";
    // No color set
    bp.add_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->customColor().has_value());
}
