// TDD Test 1: Hit test routing point
// Тест проверяет что hit test находит routing point

#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/data/pt.h"
#include "editor/data/port.h"
#include "editor/wires/hittest.h"

TEST(RoutingTest, HitRoutingPoint) {
    Blueprint bp;

    // Создаем 2 узла
    Node n1;
    n1.id = "n1";
    n1.pos = Pt(0, 0);
    n1.size = Pt(100, 80);
    n1.outputs.push_back(Port("out", PortSide::Output, an24::PortType::V));
    bp.nodes.push_back(n1);

    Node n2;
    n2.id = "n2";
    n2.pos = Pt(300, 0);
    n2.size = Pt(100, 80);
    n2.inputs.push_back(Port("in", PortSide::Input, an24::PortType::V));
    bp.nodes.push_back(n2);

    // Создаем провод с routing point
    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    w.routing_points.push_back(Pt(150.0f, 40.0f)); // Точка изгиба
    bp.wires.push_back(w);

    // Hit test рядом с routing point (в пределах радиуса 10)
    auto hit = hit_test_routing_point(bp, Pt(150.0f, 41.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->wire_index, 0);
    EXPECT_EQ(hit->routing_point_index, 0);
}

// Тест: hit test НЕ находит routing point если курсор далеко
TEST(RoutingTest, MissRoutingPoint) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.pos = Pt(0, 0);
    n1.size = Pt(100, 80);
    n1.outputs.push_back(Port("out", PortSide::Output, an24::PortType::V));
    bp.nodes.push_back(n1);

    Node n2;
    n2.id = "n2";
    n2.pos = Pt(300, 0);
    n2.size = Pt(100, 80);
    n2.inputs.push_back(Port("in", PortSide::Input, an24::PortType::V));
    bp.nodes.push_back(n2);

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    w.routing_points.push_back(Pt(150.0f, 40.0f));
    bp.wires.push_back(w);

    // Курсор далеко от routing point (более 10 пикселей)
    auto hit = hit_test_routing_point(bp, Pt(150.0f, 100.0f));
    EXPECT_FALSE(hit.has_value());
}
