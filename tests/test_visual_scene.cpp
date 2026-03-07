#include <gtest/gtest.h>
#include "editor/visual_scene.h"

// ============================================================================
// VisualScene: scene graph ownership + facade tests
// ============================================================================

TEST(VisualScene, DefaultEmpty) {
    VisualScene scene;
    EXPECT_TRUE(scene.blueprint().nodes.empty());
    EXPECT_TRUE(scene.blueprint().wires.empty());
}

TEST(VisualScene, AddNode_AppearsInBlueprint) {
    VisualScene scene;
    Node n;
    n.id = "n1";
    n.at(100, 50).size_wh(120, 80);
    n.output("out");

    size_t idx = scene.addNode(std::move(n));
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(scene.blueprint().nodes.size(), 1u);
    EXPECT_EQ(scene.blueprint().nodes[0].id, "n1");
}

TEST(VisualScene, AddNode_CreatesVisual) {
    VisualScene scene;
    Node n;
    n.id = "n1";
    n.at(100, 50).size_wh(120, 80);
    scene.addNode(std::move(n));

    auto* vis = scene.visual("n1");
    ASSERT_NE(vis, nullptr);
}

TEST(VisualScene, RemoveNode_RemovesConnectedWires) {
    VisualScene scene;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    scene.addNode(std::move(n1));
    scene.addNode(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    scene.blueprint().add_wire(std::move(w));
    EXPECT_EQ(scene.blueprint().wires.size(), 1u);

    scene.removeNode(0);  // remove "a"
    EXPECT_EQ(scene.blueprint().nodes.size(), 1u);
    EXPECT_EQ(scene.blueprint().wires.size(), 0u);  // wire removed too
}

TEST(VisualScene, RemoveWire) {
    VisualScene scene;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    scene.addNode(std::move(n1));
    scene.addNode(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    scene.blueprint().add_wire(std::move(w));
    scene.removeWire(0);
    EXPECT_TRUE(scene.blueprint().wires.empty());
}

TEST(VisualScene, HitTest_Node) {
    VisualScene scene;
    Node n;
    n.id = "n1";
    n.at(100, 50).size_wh(120, 80);
    scene.addNode(std::move(n));

    auto hit = scene.hitTest(Pt(150, 80));
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0u);
}

TEST(VisualScene, HitTest_Empty_ReturnsNone) {
    VisualScene scene;
    auto hit = scene.hitTest(Pt(100, 100));
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(VisualScene, HitTestPorts_FindsPort) {
    VisualScene scene;
    Node n;
    n.id = "n1";
    n.at(100, 50).size_wh(120, 80);
    n.input("v_in").output("v_out");
    scene.addNode(std::move(n));

    auto* vis = scene.visual("n1");
    ASSERT_NE(vis, nullptr);
    Pt port_pos = vis->getPort("v_in")->worldPosition();

    auto hit = scene.hitTestPorts(port_pos);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_name, "v_in");
}

TEST(VisualScene, PortPosition_MatchesVisual) {
    VisualScene scene;
    Node n;
    n.id = "n1";
    n.at(100, 50).size_wh(120, 80);
    n.output("out");
    scene.addNode(std::move(n));

    Pt pos = scene.portPosition(scene.blueprint().nodes[0], "out");
    auto* vis = scene.visual("n1");
    Pt vis_pos = vis->getPort("out")->worldPosition();
    EXPECT_FLOAT_EQ(pos.x, vis_pos.x);
    EXPECT_FLOAT_EQ(pos.y, vis_pos.y);
}

TEST(VisualScene, Reset_ClearsEverything) {
    VisualScene scene;
    Node n; n.id = "n1"; n.at(0, 0).size_wh(100, 50);
    scene.addNode(std::move(n));
    EXPECT_FALSE(scene.blueprint().nodes.empty());

    scene.reset();
    EXPECT_TRUE(scene.blueprint().nodes.empty());
    EXPECT_EQ(scene.visual("n1"), nullptr);
}

TEST(VisualScene, Render_DoesNotCrash) {
    VisualScene scene;
    Node n; n.id = "n1"; n.at(100, 50).size_wh(120, 80);
    n.output("out");
    scene.addNode(std::move(n));

    // Minimal IDrawList stub
    struct StubDL : IDrawList {
        void add_line(Pt, Pt, uint32_t, float) override {}
        void add_rect(Pt, Pt, uint32_t, float) override {}
        void add_rect_filled(Pt, Pt, uint32_t) override {}
        void add_circle(Pt, float, uint32_t, int) override {}
        void add_circle_filled(Pt, float, uint32_t, int) override {}
        void add_text(Pt, const char*, uint32_t, float) override {}
        void add_polyline(const Pt*, size_t, uint32_t, float) override {}
        Pt calc_text_size(const char*, float) const override { return Pt(0, 0); }
    } dl;

    scene.render(&dl, Pt(0, 0), Pt(800, 600));
    // No crash = pass
}

TEST(VisualScene, ViewportAccessible) {
    VisualScene scene;
    scene.viewport().zoom = 2.0f;
    EXPECT_FLOAT_EQ(scene.viewport().zoom, 2.0f);
}
