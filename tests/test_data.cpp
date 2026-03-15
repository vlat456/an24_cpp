#include <gtest/gtest.h>
#include "ui/math/pt.h"
#include "editor/data/port.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/data/blueprint.h"
#include "ui/core/interned_id.h"

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
    auto& I = bp.interner();
    Node n;
    n.id = I.intern("n1");
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(10.0f, 20.0f);
    bp.add_node(std::move(n));
    EXPECT_EQ(bp.nodes.size(), 1);
    EXPECT_EQ(bp.nodes[0].id, I.intern("n1"));
}

TEST(BlueprintTest, AddWire) {
    Blueprint bp;
    auto& I = bp.interner();
    Node n1;
    n1.id = I.intern("n1");
    n1.at(0.0f, 0.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = I.intern("n2");
    n2.at(100.0f, 0.0f);
    bp.add_node(std::move(n2));

    Wire w;
    w.id = I.intern("w1");
    w.start.node_id = I.intern("n1");
    w.start.port_name = I.intern("out");
    w.end.node_id = I.intern("n2");
    w.end.port_name = I.intern("in");

    bp.add_wire(std::move(w));
    EXPECT_EQ(bp.wires.size(), 1);
    EXPECT_EQ(bp.wires[0].start.node_id, I.intern("n1"));
}

TEST(BlueprintTest, FindNode) {
    Blueprint bp;
    auto& I = bp.interner();
    Node n1;
    n1.id = I.intern("batt");
    n1.at(0.0f, 0.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = I.intern("load");
    n2.at(100.0f, 0.0f);
    bp.add_node(std::move(n2));

    Node* found = bp.find_node("load");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, I.intern("load"));

    Node* not_found = bp.find_node("nonexistent");
    EXPECT_EQ(not_found, nullptr);
}
