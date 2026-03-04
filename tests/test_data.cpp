#include <gtest/gtest.h>
#include "editor/data/pt.h"
#include "editor/data/port.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/data/blueprint.h"

/// TDD Step 1: Data layer - сначала тесты
/// Запусти: cmake --build . && ctest -R test_data --output-on-failure

TEST(PtTest, DefaultIsZero) {
    Pt p;
    EXPECT_EQ(p.x, 0.0f);
    EXPECT_EQ(p.y, 0.0f);
}

TEST(PtTest, Constructor) {
    Pt p(10.0f, 20.0f);
    EXPECT_EQ(p.x, 10.0f);
    EXPECT_EQ(p.y, 20.0f);
}

TEST(PtTest, ZeroStatic) {
    Pt p = Pt::zero();
    EXPECT_EQ(p.x, 0.0f);
    EXPECT_EQ(p.y, 0.0f);
}

TEST(PtTest, Addition) {
    Pt a(10.0f, 20.0f);
    Pt b(5.0f, 30.0f);
    Pt c = a + b;
    EXPECT_EQ(c.x, 15.0f);
    EXPECT_EQ(c.y, 50.0f);
}

TEST(PtTest, Subtraction) {
    Pt a(10.0f, 20.0f);
    Pt b(5.0f, 30.0f);
    Pt c = a - b;
    EXPECT_EQ(c.x, 5.0f);
    EXPECT_EQ(c.y, -10.0f);
}

TEST(PtTest, Multiplication) {
    Pt p(10.0f, 20.0f);
    Pt c = p * 2.0f;
    EXPECT_EQ(c.x, 20.0f);
    EXPECT_EQ(c.y, 40.0f);
}

TEST(PtTest, Equality) {
    Pt a(10.0f, 20.0f);
    Pt b(10.0f, 20.0f);
    Pt c(10.0f, 21.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

// =============================================================================
// Blueprint тесты
// =============================================================================

TEST(BlueprintTest, DefaultIsEmpty) {
    Blueprint bp;
    EXPECT_TRUE(bp.nodes.empty());
    EXPECT_TRUE(bp.wires.empty());
    EXPECT_EQ(bp.zoom, 1.0f);
    EXPECT_EQ(bp.grid_step, 16.0f);
}

TEST(BlueprintTest, AddNode) {
    Blueprint bp;
    Node n;
    n.id = "n1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(10.0f, 20.0f);
    bp.add_node(std::move(n));
    EXPECT_EQ(bp.nodes.size(), 1);
    EXPECT_EQ(bp.nodes[0].id, "n1");
}

TEST(BlueprintTest, AddWire) {
    Blueprint bp;
    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(100.0f, 0.0f);
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";

    bp.add_wire(std::move(w));
    EXPECT_EQ(bp.wires.size(), 1);
    EXPECT_EQ(bp.wires[0].start.node_id, "n1");
}

TEST(BlueprintTest, FindNode) {
    Blueprint bp;
    Node n1;
    n1.id = "batt";
    n1.at(0.0f, 0.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "load";
    n2.at(100.0f, 0.0f);
    bp.add_node(std::move(n2));

    Node* found = bp.find_node("load");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, "load");

    Node* not_found = bp.find_node("nonexistent");
    EXPECT_EQ(not_found, nullptr);
}
