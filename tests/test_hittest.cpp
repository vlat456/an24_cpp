#include <gtest/gtest.h>
#include "editor/hittest.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport.h"
#include "editor/visual_node.h"

/// TDD Step 6: Hit testing

TEST(HitTest, EmptyBlueprint_ReturnsNone) {
    Blueprint bp;
    Viewport vp;
    auto hit = hit_test(bp, Pt(100.0f, 100.0f), vp);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(HitTest, Node_Inside_ReturnsNode) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    Viewport vp;
    // Клик внутри узла
    auto hit = hit_test(bp, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0);
}

TEST(HitTest, Node_Outside_ReturnsNone) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    Viewport vp;
    // Клик вне узла
    auto hit = hit_test(bp, Pt(0.0f, 0.0f), vp);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(HitTest, MultipleNodes_ReturnsClosest) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f);
    n1.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(200.0f, 0.0f);
    n2.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n2));

    Viewport vp;
    // Клик между узлами - первый найденный
    auto hit = hit_test(bp, Pt(50.0f, 25.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0);
}

// ============================================================================
// Port Hit Testing (TDD for wire creation)
// ============================================================================

TEST(PortHitTest, EmptyBlueprint_NoPorts) {
    Blueprint bp;
    VisualNodeCache cache;
    Viewport vp;

    auto hit = hit_test_ports(bp, cache, Pt(100.0f, 100.0f));
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(PortHitTest, NodeWithPorts_ClickNearPort_ReturnsPort) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    Viewport vp;

    // Проверка что порты созданы (нужно знать позицию портов)
    auto* visual = cache.get("batt1");
    ASSERT_NE(visual, nullptr);

    // Получаем позицию входного порта
    Pt port_pos = visual->getPortPosition("v_in");

    // Кликаем прямо на порт
    auto hit = hit_test_ports(bp, cache, port_pos);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_node_id, "batt1");
    EXPECT_EQ(hit.port_name, "v_in");
}

TEST(PortHitTest, ClickFarFromPorts_ReturnsNone) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    Viewport vp;

    // Кликаем далеко от портов
    auto hit = hit_test_ports(bp, cache, Pt(1000.0f, 1000.0f));
    EXPECT_EQ(hit.type, HitType::None);
}
