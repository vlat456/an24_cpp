#include <gtest/gtest.h>
#include "editor/visual/node/node.h"
#include "editor/visual/node/types/bus_node.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"


// ============================================================================
// BusVisualNode::swapAliasPorts() - TDD Failing Test First
// ============================================================================

TEST(BusPortSwapTest, SwapTwoAliasPorts_ChangesOrder) {
    // Create a bus node
    Node bus;
    bus.id = "bus1";
    bus.name = "MainBus";
    bus.type_name = "bus";
    bus.render_hint = "bus";
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v");
    bus.output("v");

    // Create three wires attached to the bus
    Wire w1 = Wire::make("wire_1", wire_output("node_a", "out"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("wire_2", wire_output("node_b", "out"), wire_input("bus1", "v"));
    Wire w3 = Wire::make("wire_3", wire_output("node_c", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2, w3};

    // Create BusVisualNode with wires
    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Verify initial port order: [wire_1, wire_2, wire_3, "v"]
    ASSERT_EQ(bus_visual.getPortCount(), 4u);
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_1");
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_2");
    EXPECT_EQ(bus_visual.getPort(2)->name(), "wire_3");
    EXPECT_EQ(bus_visual.getPort(3)->name(), "v");

    // Swap wire_1 and wire_3
    bool swapped = bus_visual.swapAliasPorts("wire_1", "wire_3");

    // EXPECTED: swap should succeed
    EXPECT_TRUE(swapped) << "swapAliasPorts should return true for valid wire IDs";

    // EXPECTED: port order should now be: [wire_3, wire_2, wire_1, "v"]
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_3")
        << "After swap, port[0] should be wire_3";
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_2")
        << "Port[1] should remain wire_2 (not involved in swap)";
    EXPECT_EQ(bus_visual.getPort(2)->name(), "wire_1")
        << "After swap, port[2] should be wire_1";
    EXPECT_EQ(bus_visual.getPort(3)->name(), "v")
        << "Logical 'v' port should remain at the end";
}

TEST(BusPortSwapTest, SwapSamePort_ReturnsFalse) {
    // Create a bus node with wires
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.render_hint = "bus";
    bus.at(0, 0).size_wh(80, 32);
    bus.input("v");
    bus.output("v");

    Wire w1 = Wire::make("wire_1", wire_output("a", "out"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("wire_2", wire_output("b", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2};

    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Try to swap a port with itself
    bool swapped = bus_visual.swapAliasPorts("wire_1", "wire_1");

    // EXPECTED: should return false (no-op)
    EXPECT_FALSE(swapped) << "Swapping a port with itself should return false";

    // Port order should remain unchanged
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_1");
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_2");
}

TEST(BusPortSwapTest, SwapNonExistentPort_ReturnsFalse) {
    // Create a bus node
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.render_hint = "bus";
    bus.at(0, 0).size_wh(80, 32);
    bus.input("v");
    bus.output("v");

    Wire w1 = Wire::make("wire_1", wire_output("a", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1};

    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Try to swap with a non-existent wire
    bool swapped = bus_visual.swapAliasPorts("wire_1", "ghost_wire");

    // EXPECTED: should return false (ghost_wire doesn't exist)
    EXPECT_FALSE(swapped) << "Swapping with non-existent port should return false";

    // Port order should remain unchanged
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_1");
}

TEST(BusPortSwapTest, SwapAdjacentPorts) {
    // Create a bus node
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.render_hint = "bus";
    bus.at(0, 0).size_wh(80, 32);
    bus.input("v");
    bus.output("v");

    Wire w1 = Wire::make("wire_1", wire_output("a", "out"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("wire_2", wire_output("b", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2};

    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Swap adjacent ports
    bool swapped = bus_visual.swapAliasPorts("wire_1", "wire_2");

    EXPECT_TRUE(swapped);

    // Port order should be reversed
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_2");
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_1");
}

TEST(BusPortSwapTest, HandlePortSwap_CallsVirtualMethod) {
    // Create a bus node
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.render_hint = "bus";
    bus.at(0, 0).size_wh(80, 32);
    bus.input("v");
    bus.output("v");

    Wire w1 = Wire::make("wire_1", wire_output("a", "out"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("wire_2", wire_output("b", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2};

    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Test through the virtual interface (polymorphism)
    VisualNode* base = &bus_visual;

    // Call virtual method - BusVisualNode should override this
    bool handled = base->handlePortSwap("wire_1", "wire_2");

    EXPECT_TRUE(handled) << "BusVisualNode::handlePortSwap should return true";

    // Verify swap happened
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_2");
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_1");
}

TEST(BusPortSwapTest, HandlePortSwap_StandardNodeReturnsFalse) {
    // Create a standard (non-bus) node
    Node node;
    node.id = "node1";
    node.name = "Battery";
    node.type_name = "battery";
    node.at(0, 0).size_wh(120, 80);
    node.input("v_in");
    node.output("v_out");

    VisualNode visual(node);

    // Standard nodes don't support port swapping
    bool handled = visual.handlePortSwap("v_in", "v_out");

    EXPECT_FALSE(handled) << "Standard VisualNode::handlePortSwap should return false";
}

TEST(BusPortSwapTest, SwapPreservesPortPositions) {
    // Create a bus node
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.render_hint = "bus";
    bus.at(100, 50).size_wh(96, 32);
    bus.input("v");
    bus.output("v");

    Wire w1 = Wire::make("wire_1", wire_output("a", "out"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("wire_2", wire_output("b", "out"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2};

    BusVisualNode bus_visual(bus, BusOrientation::Horizontal, wires);

    // Get original port positions
    Pt pos_0_before = bus_visual.getPort(0)->worldPosition();
    Pt pos_1_before = bus_visual.getPort(1)->worldPosition();

    // Swap ports - this swaps WHICH wires are at WHICH positions
    bus_visual.swapAliasPorts("wire_1", "wire_2");

    // Get positions after swap
    Pt pos_0_after = bus_visual.getPort(0)->worldPosition();
    Pt pos_1_after = bus_visual.getPort(1)->worldPosition();

    // EXPECTED: slot positions remain the same (slots don't move)
    // but now port[0] has wire_2 and port[1] has wire_1
    EXPECT_FLOAT_EQ(pos_0_after.x, pos_0_before.x)
        << "Port slot [0] position should remain unchanged";
    EXPECT_FLOAT_EQ(pos_0_after.y, pos_0_before.y);
    EXPECT_FLOAT_EQ(pos_1_after.x, pos_1_before.x)
        << "Port slot [1] position should remain unchanged";
    EXPECT_FLOAT_EQ(pos_1_after.y, pos_1_before.y);

    // Verify that names DID swap (wire IDs at each position changed)
    EXPECT_EQ(bus_visual.getPort(0)->name(), "wire_2")
        << "Slot [0] should now have wire_2";
    EXPECT_EQ(bus_visual.getPort(1)->name(), "wire_1")
        << "Slot [1] should now have wire_1";
}
