#include <gtest/gtest.h>
#include "visual/scene_mutations.h"
#include "visual/scene.h"
#include "visual/node/node_factory.h"
#include "visual/wire/wire.h"
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
// REGRESSION: add_wire rollback doesn't use moved-from wire
// ============================================================================
// The add_wire function moves the wire into add_wire_validated(). If validation
// fails, the rollback must use wire_copy (saved before the move) to call
// disconnectWire() on bus nodes. Before the fix, the moved-from wire object
// was accessed, causing undefined behavior.

TEST(SceneMutations, AddWireRollbackPreservesBusWires) {
    Blueprint bp;

    // Bus node
    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    // Two regular nodes
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

    // Existing wire: bat1:v_out -> bus1:v
    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));

    // Existing wire: bus1:v -> lamp1:v_in (occupies lamp1:v_in)
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 3 nodes + 2 wires = 5
    EXPECT_EQ(scene.roots().size(), 5u);
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);

    // Third node whose output is not yet occupied
    Node n3;
    n3.id = "gen1"; n3.name = "Generator"; n3.type_name = "Battery";
    n3.output("v_out", PortType::V);
    visual::mutations::add_node(scene, bp, std::move(n3), "");

    // Try to add a wire from gen1:v_out -> lamp1:v_in
    // This should fail: lamp1:v_in is already occupied (one-to-one constraint)
    auto bad_wire = Wire::make("w_bad",
        wire_output("gen1", "v_out"),
        wire_input("lamp1", "v_in"));
    bool ok = visual::mutations::add_wire(scene, bp, std::move(bad_wire), "");

    EXPECT_FALSE(ok);
    // Blueprint should still have exactly 2 wires (no w_bad added)
    EXPECT_EQ(bp.wires.size(), 2u);
    // The original wire widgets must still exist
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);
    // No widget for the rejected wire
    EXPECT_EQ(scene.find("w_bad"), nullptr);
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

// ============================================================================
// REGRESSION: Wire destructor must not access freed WireEnd after rebuildPorts
// ============================================================================
// When rebuildPorts() destroys bus ports, the WireEnd children are freed.
// The original code only cleared the WireEnd→Wire back-pointer, but did NOT
// null out the Wire→WireEnd pointer, leaving a dangling pointer in the Wire.
// When flushRemovals() later destroyed the Wire, its destructor called
// start_->clearWire() through the dangling pointer — use-after-free.
//
// The fix adds Wire::detachEndpoint() which is called from rebuildPorts()
// to null out the Wire's pointer to the WireEnd being destroyed.

TEST(SceneMutations, SwapBusWirePortsNoDanglingPointer) {
    Blueprint bp;

    // Bus node
    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    // Two regular nodes
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

    // Two wires both touching bus1:v
    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    EXPECT_EQ(scene.roots().size(), 5u); // 3 nodes + 2 wires

    // Swap alias ports — this calls rebuildPorts() which destroys old
    // WireEnds, then recreate_bus_wires destroys and recreates wire widgets.
    // Before the fix, the Wire destructor would use-after-free on the
    // dangling WireEnd pointer.
    bool ok = visual::mutations::swap_wire_ports_on_bus(scene, bp, "bus1", "w0", "w1");
    EXPECT_TRUE(ok);

    // Both wires should still exist after the swap
    scene.flushRemovals();
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);
}

// REGRESSION: reconnect_wire via bus does not access freed WireEnd
TEST(SceneMutations, ReconnectWireViaBusNoDanglingPointer) {
    Blueprint bp;

    // Bus node
    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    // Source and two loads
    Node n1;
    n1.id = "bat1"; n1.name = "Bat"; n1.type_name = "Battery";
    n1.group_id = "";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "L1"; n2.type_name = "Lamp";
    n2.group_id = "";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Node n3;
    n3.id = "lamp2"; n3.name = "L2"; n3.type_name = "Lamp";
    n3.group_id = "";
    n3.input("v_in", PortType::V);
    bp.add_node(std::move(n3));

    // Two wires: bat→bus, bus→lamp1
    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    EXPECT_EQ(scene.roots().size(), 6u); // 4 nodes + 2 wires

    // Reconnect w1's end from lamp1 to lamp2 — bus1 rebuilds ports,
    // destroying old WireEnds. Must not crash via dangling pointer.
    WireEnd new_end("lamp2", "v_in", PortSide::Input);
    visual::mutations::reconnect_wire(scene, bp, 1, false, new_end, "");
    scene.flushRemovals();

    // w1 should point to lamp2 now
    EXPECT_NE(scene.find("w1"), nullptr);
    EXPECT_EQ(bp.wires[1].end.node_id, "lamp2");
}

// ============================================================================
// Bus Wire Index tests
// ============================================================================

TEST(SceneMutations, BusWireIndex_PopulatedAfterRebuild) {
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

    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // Index should have bus1 → [w0]
    const auto& bus_wires = bp.busWires("bus1");
    EXPECT_EQ(bus_wires.size(), 1u);
    EXPECT_EQ(bus_wires[0], "w0");

    // Non-bus should return empty
    EXPECT_EQ(bp.busWires("bat1").size(), 0u);
    EXPECT_EQ(bp.busWires("nonexistent").size(), 0u);
}

TEST(SceneMutations, BusWireIndex_UpdatedAfterAddWire) {
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

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // Add first wire
    auto w0 = Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    visual::mutations::add_wire(scene, bp, std::move(w0), "");

    EXPECT_EQ(bp.busWires("bus1").size(), 1u);

    // Add second wire
    auto w1 = Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in"));
    visual::mutations::add_wire(scene, bp, std::move(w1), "");

    EXPECT_EQ(bp.busWires("bus1").size(), 2u);
}

TEST(SceneMutations, BusWireIndex_UpdatedAfterRemoveWire) {
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

    EXPECT_EQ(bp.busWires("bus1").size(), 2u);

    // Remove w0
    visual::mutations::remove_wire(scene, bp, 0);
    scene.flushRemovals();

    EXPECT_EQ(bp.busWires("bus1").size(), 1u);
    EXPECT_EQ(bp.busWires("bus1")[0], "w1");
}

// Regression: rebuild_bus_wire_index must be called after bulk wire loading.
// Simulates the from_flat() path where wires are pushed directly into bp.wires
// and only rebuild_wire_index() + rebuild_bus_wire_index() are called afterward.
TEST(SceneMutations, BusWireIndex_PopulatedAfterBulkLoadAndRebuild) {
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

    // Push wires directly (mimicking from_flat bulk path, bypassing add_wire)
    Wire w;
    w.id = "w0";
    w.start.node_id = "bat1"; w.start.port_name = "v_out"; w.start.side = PortSide::Output;
    w.end.node_id = "bus1"; w.end.port_name = "v"; w.end.side = PortSide::Input;
    bp.wires.push_back(std::move(w));

    // Before rebuild, bus index should be empty
    EXPECT_EQ(bp.busWires("bus1").size(), 0u);

    // Rebuild both indices (as from_flat should)
    bp.rebuild_wire_index();
    bp.rebuild_bus_wire_index();
    bp.rebuild_port_occupancy_index();

    // Now the bus index should be populated
    EXPECT_EQ(bp.busWires("bus1").size(), 1u);
    EXPECT_EQ(bp.busWires("bus1")[0], "w0");
}

// ============================================================================
// Node Index tests
// ============================================================================

TEST(SceneMutations, NodeIndex_PopulatedAfterAddNode) {
    Blueprint bp;
    
    Node n1;
    n1.id = "bat1"; n1.name = "Battery"; n1.type_name = "Battery";
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.name = "Lamp"; n2.type_name = "Lamp";
    bp.add_node(std::move(n2));

    // Index should have both nodes
    EXPECT_EQ(bp.node_index_.size(), 2u);
    EXPECT_EQ(bp.node_index_.count("bat1"), 1u);
    EXPECT_EQ(bp.node_index_.count("lamp1"), 1u);
    EXPECT_EQ(bp.node_index_["bat1"], 0u);
    EXPECT_EQ(bp.node_index_["lamp1"], 1u);
}

TEST(SceneMutations, NodeIndex_FindNodeUsesIndex) {
    Blueprint bp;
    
    // Add 100 nodes to make O(N) vs O(1) noticeable
    for (int i = 0; i < 100; ++i) {
        Node n;
        n.id = "node_" + std::to_string(i);
        n.type_name = "Test";
        bp.add_node(std::move(n));
    }

    // find_node should find each node by ID
    for (int i = 0; i < 100; ++i) {
        std::string id = "node_" + std::to_string(i);
        const Node* n = bp.find_node(id.c_str());
        ASSERT_NE(n, nullptr);
        EXPECT_EQ(n->id, id);
    }

    // Non-existent node should return nullptr
    EXPECT_EQ(bp.find_node("nonexistent"), nullptr);
}

TEST(SceneMutations, NodeIndex_AddDuplicateReturnsExistingIndex) {
    Blueprint bp;
    
    Node n1;
    n1.id = "bat1"; n1.type_name = "Battery";
    size_t idx1 = bp.add_node(std::move(n1));

    Node n2;
    n2.id = "bat1"; n2.type_name = "Lamp";  // Same ID, different type
    size_t idx2 = bp.add_node(std::move(n2));

    // Should return same index, not add duplicate
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.node_index_.size(), 1u);
    // Original node should NOT be replaced
    EXPECT_EQ(bp.nodes[0].type_name, "Battery");
}

TEST(SceneMutations, NodeIndex_RebuiltAfterRemoveNodes) {
    Blueprint bp;
    
    Node n1;
    n1.id = "bat1"; n1.type_name = "Battery";
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "lamp1"; n2.type_name = "Lamp";
    bp.add_node(std::move(n2));

    Node n3;
    n3.id = "gen1"; n3.type_name = "Generator";
    bp.add_node(std::move(n3));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    EXPECT_EQ(bp.node_index_.size(), 3u);

    // Remove node at index 1 (lamp1)
    visual::mutations::remove_nodes(scene, bp, {1});
    scene.flushRemovals();

    // Index should be rebuilt with remaining nodes
    EXPECT_EQ(bp.nodes.size(), 2u);
    EXPECT_EQ(bp.node_index_.size(), 2u);
    EXPECT_EQ(bp.node_index_.count("lamp1"), 0u);  // lamp1 removed
    EXPECT_NE(bp.find_node("bat1"), nullptr);
    EXPECT_NE(bp.find_node("gen1"), nullptr);
    EXPECT_EQ(bp.find_node("lamp1"), nullptr);
}

TEST(SceneMutations, NodeIndex_RebuiltAfterBulkLoad) {
    Blueprint bp;

    // Simulate bulk load (like from_flat)
    Node n1;
    n1.id = "node_a"; n1.type_name = "Test";
    bp.nodes.push_back(std::move(n1));

    Node n2;
    n2.id = "node_b"; n2.type_name = "Test";
    bp.nodes.push_back(std::move(n2));

    // Index is empty before rebuild
    EXPECT_EQ(bp.node_index_.size(), 0u);
    EXPECT_EQ(bp.find_node("node_a"), nullptr);  // O(1) lookup fails

    // Rebuild the index
    bp.rebuild_node_index();

    // Index should now be populated
    EXPECT_EQ(bp.node_index_.size(), 2u);
    EXPECT_NE(bp.find_node("node_a"), nullptr);
    EXPECT_NE(bp.find_node("node_b"), nullptr);
}

// ============================================================================
// Port Occupancy Index
// ============================================================================

TEST(SceneMutations, PortOccupancy_EmptyByDefault) {
    Blueprint bp;
    EXPECT_FALSE(bp.is_port_occupied("any_node", "any_port"));
}

TEST(SceneMutations, PortOccupancy_PopulatedAfterAddWire) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));

    EXPECT_TRUE(bp.is_port_occupied("bat1", "v_out"));
    EXPECT_TRUE(bp.is_port_occupied("lamp1", "v_in"));
    EXPECT_FALSE(bp.is_port_occupied("bat1", "v_in"));  // non-existent port
}

TEST(SceneMutations, PortOccupancy_ClearedAfterRemoveWire) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));
    EXPECT_TRUE(bp.is_port_occupied("bat1", "v_out"));

    bp.remove_wire_at(0);
    EXPECT_FALSE(bp.is_port_occupied("bat1", "v_out"));
    EXPECT_FALSE(bp.is_port_occupied("lamp1", "v_in"));
}

TEST(SceneMutations, PortOccupancy_BlocksSecondWireOnSamePort) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Node n3; n3.id = "lamp2"; n3.type_name = "Lamp";
    n3.input("v_in", PortType::V);
    bp.add_node(std::move(n3));

    Wire w1 = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("lamp1", "v_in"));
    EXPECT_TRUE(bp.add_wire_validated(std::move(w1)));

    // Second wire to same output port should be rejected
    Wire w2 = Wire::make("w1", wire_output("bat1", "v_out"), wire_input("lamp2", "v_in"));
    EXPECT_FALSE(bp.add_wire_validated(std::move(w2)));
}

TEST(SceneMutations, PortOccupancy_BusAllowsMultiple) {
    Blueprint bp;
    Node bus; bus.id = "bus1"; bus.type_name = "Bus";
    bus.input("alias_0", PortType::V);
    bus.input("alias_1", PortType::V);
    bp.add_node(std::move(bus));

    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "bat2"; n2.type_name = "Battery";
    n2.output("v_out", PortType::V);
    bp.add_node(std::move(n2));

    Wire w1 = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("bus1", "alias_0"));
    EXPECT_TRUE(bp.add_wire_validated(std::move(w1)));

    Wire w2 = Wire::make("w1", wire_output("bat2", "v_out"), wire_input("bus1", "alias_1"));
    EXPECT_TRUE(bp.add_wire_validated(std::move(w2)));

    // Bus should have both wires tracked
    EXPECT_TRUE(bp.is_port_occupied("bus1", "alias_0"));
    EXPECT_TRUE(bp.is_port_occupied("bus1", "alias_1"));
}

TEST(SceneMutations, PortOccupancy_RebuiltAfterBulkLoad) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.nodes.push_back(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.nodes.push_back(std::move(n2));

    bp.rebuild_node_index();

    // Bulk-load wire (bypassing add_wire)
    Wire w;
    w.id = "w0";
    w.start = WireEnd("bat1", "v_out", PortSide::Output);
    w.end = WireEnd("lamp1", "v_in", PortSide::Input);
    bp.wires.push_back(std::move(w));

    // Index is empty before rebuild
    EXPECT_FALSE(bp.is_port_occupied("bat1", "v_out"));

    // Rebuild
    bp.rebuild_port_occupancy_index();

    // Now populated
    EXPECT_TRUE(bp.is_port_occupied("bat1", "v_out"));
    EXPECT_TRUE(bp.is_port_occupied("lamp1", "v_in"));
}

TEST(SceneMutations, PortOccupancy_UpdatedAfterReconnect) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Node n3; n3.id = "lamp2"; n3.type_name = "Lamp";
    n3.input("v_in", PortType::V);
    bp.add_node(std::move(n3));

    Wire w = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w));

    EXPECT_TRUE(bp.is_port_occupied("lamp1", "v_in"));
    EXPECT_FALSE(bp.is_port_occupied("lamp2", "v_in"));

    // Simulate reconnect: change end endpoint
    Wire old_wire = bp.wires[0];
    bp.wires[0].end = WireEnd("lamp2", "v_in", PortSide::Input);
    bp.rekey_wire(old_wire, bp.wires[0]);
    bp.updatePortOccupancyForEndpoints(old_wire, bp.wires[0]);

    EXPECT_FALSE(bp.is_port_occupied("lamp1", "v_in"));
    EXPECT_TRUE(bp.is_port_occupied("lamp2", "v_in"));
    EXPECT_TRUE(bp.is_port_occupied("bat1", "v_out"));  // unchanged
}

TEST(SceneMutations, PortOccupancy_ConsistentAfterRemoveNodes) {
    Blueprint bp;
    Node n1; n1.id = "bat1"; n1.type_name = "Battery";
    n1.output("v_out", PortType::V);
    bp.add_node(std::move(n1));

    Node n2; n2.id = "lamp1"; n2.type_name = "Lamp";
    n2.input("v_in", PortType::V);
    bp.add_node(std::move(n2));

    Node n3; n3.id = "lamp2"; n3.type_name = "Lamp";
    n3.input("v_in", PortType::V);
    n3.output("v_out", PortType::V);
    bp.add_node(std::move(n3));

    Wire w1 = Wire::make("w0", wire_output("bat1", "v_out"), wire_input("lamp1", "v_in"));
    bp.add_wire(std::move(w1));

    Wire w2 = Wire::make("w1", wire_output("bat1", "v_out"), wire_input("lamp2", "v_in"));
    bp.add_wire(std::move(w2));

    // Remove lamp1 — its wire should be cleaned up
    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");
    visual::mutations::remove_nodes(scene, bp, {1});  // lamp1 is at index 1

    // lamp1's port should no longer be occupied
    EXPECT_FALSE(bp.is_port_occupied("lamp1", "v_in"));
    // bat1's port should still be occupied (w1 to lamp2 survived)
    EXPECT_TRUE(bp.is_port_occupied("bat1", "v_out"));
    EXPECT_TRUE(bp.is_port_occupied("lamp2", "v_in"));
}

// ============================================================================
// Two-bus-node scenarios (shared wires should not be double-created)
// ============================================================================

// REGRESSION: Adding a wire between two bus nodes that share other wires
// must not create duplicate Wire widgets for the shared wires.
TEST(SceneMutations, AddWireTwoBusNodes_NoDoubleCreation) {
    Blueprint bp;

    // Bus A
    Node busA;
    busA.id = "busA"; busA.name = "BusA"; busA.type_name = "Bus";
    busA.render_hint = "bus";
    busA.group_id = "";
    busA.size_wh(200, 40);
    busA.input("v", PortType::V);
    bp.add_node(std::move(busA));

    // Bus B
    Node busB;
    busB.id = "busB"; busB.name = "BusB"; busB.type_name = "Bus";
    busB.render_hint = "bus";
    busB.group_id = "";
    busB.size_wh(200, 40);
    busB.input("v", PortType::V);
    bp.add_node(std::move(busB));

    // Regular node
    Node bat;
    bat.id = "bat1"; bat.name = "Bat"; bat.type_name = "Battery";
    bat.group_id = "";
    bat.output("v_out", PortType::V);
    bp.add_node(std::move(bat));

    // Wire linking the two buses (shared wire)
    bp.add_wire(Wire::make("w0",
        wire_output("busA", "v"),
        wire_input("busB", "v")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // 3 nodes + 1 wire
    EXPECT_EQ(scene.roots().size(), 4u);
    EXPECT_NE(scene.find("w0"), nullptr);

    // Add a second wire: bat1 -> busA. This triggers rebuildPorts on busA,
    // which orphans w0. recreate_bus_wires is called for busA and busB.
    // Without the fix, w0 would be recreated TWICE (once per bus).
    bool ok = visual::mutations::add_wire(scene, bp,
        Wire::make("w1", wire_output("bat1", "v_out"), wire_input("busA", "v")),
        "");
    EXPECT_TRUE(ok);

    scene.flushRemovals();

    // Should have: 3 nodes + 2 wires = 5 roots (not 6!)
    EXPECT_EQ(scene.roots().size(), 5u);

    // Both wires must be findable
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);

    // Count Wire widgets explicitly — there should be exactly 2
    size_t wire_count = 0;
    for (const auto& r : scene.roots()) {
        if (dynamic_cast<visual::Wire*>(r.get())) wire_count++;
    }
    EXPECT_EQ(wire_count, 2u);
}

// REGRESSION: Removing a wire between two bus nodes that share other wires
// must not create duplicate Wire widgets.
TEST(SceneMutations, RemoveWireTwoBusNodes_NoDoubleCreation) {
    Blueprint bp;

    // Two bus nodes
    Node busA;
    busA.id = "busA"; busA.name = "BusA"; busA.type_name = "Bus";
    busA.render_hint = "bus";
    busA.group_id = "";
    busA.size_wh(200, 40);
    busA.input("v", PortType::V);
    bp.add_node(std::move(busA));

    Node busB;
    busB.id = "busB"; busB.name = "BusB"; busB.type_name = "Bus";
    busB.render_hint = "bus";
    busB.group_id = "";
    busB.size_wh(200, 40);
    busB.input("v", PortType::V);
    bp.add_node(std::move(busB));

    Node bat;
    bat.id = "bat1"; bat.name = "Bat"; bat.type_name = "Battery";
    bat.group_id = "";
    bat.output("v_out", PortType::V);
    bp.add_node(std::move(bat));

    // Two wires: one linking buses, one from bat to busA
    bp.add_wire(Wire::make("w0",
        wire_output("busA", "v"),
        wire_input("busB", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("busA", "v")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    EXPECT_EQ(scene.roots().size(), 5u); // 3 nodes + 2 wires

    // Remove w1 (bat->busA). Both buses are notified (busA has w1 removed,
    // busB shares w0 with busA). Without fix, w0 would be double-created.
    visual::mutations::remove_wire(scene, bp, 1); // w1 is at index 1

    scene.flushRemovals();

    // 3 nodes + 1 wire = 4 roots
    EXPECT_EQ(scene.roots().size(), 4u);
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_EQ(scene.find("w1"), nullptr);

    size_t wire_count = 0;
    for (const auto& r : scene.roots()) {
        if (dynamic_cast<visual::Wire*>(r.get())) wire_count++;
    }
    EXPECT_EQ(wire_count, 1u);
}

// Bus swap with alias port ID collision: scene.find(wire_id) must return
// the Wire widget, not the alias Port, after rebuildPorts + recreate.
TEST(SceneMutations, SwapBusWirePorts_FindReturnsWireNotPort) {
    Blueprint bp;

    Node bus;
    bus.id = "bus1"; bus.name = "Bus"; bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.group_id = "";
    bus.size_wh(200, 40);
    bus.input("v", PortType::V);
    bp.add_node(std::move(bus));

    Node bat;
    bat.id = "bat1"; bat.name = "Bat"; bat.type_name = "Battery";
    bat.group_id = "";
    bat.output("v_out", PortType::V);
    bp.add_node(std::move(bat));

    Node lamp;
    lamp.id = "lamp1"; lamp.name = "Lamp"; lamp.type_name = "Lamp";
    lamp.group_id = "";
    lamp.input("v_in", PortType::V);
    bp.add_node(std::move(lamp));

    bp.add_wire(Wire::make("w0",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    bp.add_wire(Wire::make("w1",
        wire_output("bus1", "v"),
        wire_input("lamp1", "v_in")));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, "");

    // After rebuild, wire widgets should be findable and be actual Wires
    auto* w0 = scene.find("w0");
    ASSERT_NE(w0, nullptr);
    EXPECT_NE(dynamic_cast<visual::Wire*>(w0), nullptr);

    auto* w1 = scene.find("w1");
    ASSERT_NE(w1, nullptr);
    EXPECT_NE(dynamic_cast<visual::Wire*>(w1), nullptr);

    // Swap — this triggers rebuildPorts (destroys/recreates alias ports)
    bool ok = visual::mutations::swap_wire_ports_on_bus(scene, bp, "bus1", "w0", "w1");
    EXPECT_TRUE(ok);

    scene.flushRemovals();

    // After swap, find() must still return Wire*, not Port*
    auto* w0_after = scene.find("w0");
    ASSERT_NE(w0_after, nullptr);
    EXPECT_NE(dynamic_cast<visual::Wire*>(w0_after), nullptr)
        << "scene.find('w0') returned a Port instead of a Wire (isIndexable bug)";

    auto* w1_after = scene.find("w1");
    ASSERT_NE(w1_after, nullptr);
    EXPECT_NE(dynamic_cast<visual::Wire*>(w1_after), nullptr)
        << "scene.find('w1') returned a Port instead of a Wire (isIndexable bug)";

    EXPECT_EQ(scene.roots().size(), 5u); // 3 nodes + 2 wires
}
