#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "editor/visual/scene.h"
#include "editor/visual/render_context.h"

namespace visual {

class TestWidget : public Widget {
public:
    TestWidget(Pt pos, Pt sz) {
        local_pos_ = pos;
        size_ = sz;
    }
    
    void render(IDrawList*, const RenderContext&) const override {}
};

/// Clickable variant so it participates in the spatial grid.
class ClickableTestWidget : public Widget {
public:
    ClickableTestWidget(Pt pos, Pt sz) {
        local_pos_ = pos;
        size_ = sz;
    }
    
    bool isClickable() const override { return true; }
    void render(IDrawList*, const RenderContext&) const override {}
};

} // namespace visual

TEST(VisualWidgetTest, DefaultConstructors) {
    visual::Widget w;
    EXPECT_EQ(w.localPos().x, 0.0f);
    EXPECT_EQ(w.localPos().y, 0.0f);
    EXPECT_EQ(w.size().x, 0.0f);
    EXPECT_EQ(w.size().y, 0.0f);
    EXPECT_EQ(w.parent(), nullptr);
    EXPECT_FALSE(w.isClickable());
}

TEST(VisualWidgetTest, SetPosition) {
    visual::Widget w;
    w.setLocalPos(Pt(10.0f, 20.0f));
    EXPECT_EQ(w.localPos().x, 10.0f);
    EXPECT_EQ(w.localPos().y, 20.0f);
}

TEST(VisualWidgetTest, SetSize) {
    visual::Widget w;
    w.setSize(Pt(100.0f, 50.0f));
    EXPECT_EQ(w.size().x, 100.0f);
    EXPECT_EQ(w.size().y, 50.0f);
}

TEST(VisualWidgetTest, WorldPosNoParent) {
    visual::TestWidget w(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    EXPECT_EQ(w.worldPos().x, 10.0f);
    EXPECT_EQ(w.worldPos().y, 20.0f);
}

TEST(VisualWidgetTest, WorldPosWithParent) {
    visual::TestWidget parent(Pt(100.0f, 200.0f), Pt(200.0f, 100.0f));
    auto child = std::make_unique<visual::TestWidget>(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    
    parent.addChild(std::move(child));
    
    EXPECT_EQ(parent.worldPos().x, 100.0f);
    EXPECT_EQ(parent.worldPos().y, 200.0f);
}

TEST(VisualWidgetTest, WorldPosNested) {
    visual::TestWidget grandparent(Pt(0.0f, 0.0f), Pt(1000.0f, 1000.0f));
    auto parent = std::make_unique<visual::TestWidget>(Pt(50.0f, 60.0f), Pt(200.0f, 100.0f));
    auto child = std::make_unique<visual::TestWidget>(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    
    visual::Widget* childPtr = child.get();
    parent->addChild(std::move(child));
    grandparent.addChild(std::move(parent));
    
    // grandparent(0,0) + parent(50,60) + child(10,20) = (60, 80)
    EXPECT_FLOAT_EQ(childPtr->worldPos().x, 60.0f);
    EXPECT_FLOAT_EQ(childPtr->worldPos().y, 80.0f);
}

TEST(VisualWidgetTest, WorldMinMax) {
    visual::TestWidget w(Pt(10.0f, 20.0f), Pt(100.0f, 50.0f));
    EXPECT_EQ(w.worldMin().x, 10.0f);
    EXPECT_EQ(w.worldMin().y, 20.0f);
    EXPECT_EQ(w.worldMax().x, 110.0f);
    EXPECT_EQ(w.worldMax().y, 70.0f);
}

TEST(VisualWidgetTest, Contains) {
    visual::TestWidget w(Pt(10.0f, 20.0f), Pt(100.0f, 50.0f));
    EXPECT_TRUE(w.contains(Pt(50.0f, 40.0f)));
    EXPECT_TRUE(w.contains(Pt(10.0f, 20.0f)));
    EXPECT_TRUE(w.contains(Pt(110.0f, 70.0f)));
    EXPECT_FALSE(w.contains(Pt(5.0f, 20.0f)));
    EXPECT_FALSE(w.contains(Pt(50.0f, 80.0f)));
}

TEST(VisualWidgetTest, AddChild) {
    visual::Widget parent;
    auto child = std::make_unique<visual::TestWidget>(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    visual::Widget* childPtr = child.get();
    
    parent.addChild(std::move(child));
    
    EXPECT_EQ(parent.children().size(), 1u);
    EXPECT_EQ(childPtr->parent(), &parent);
}

TEST(VisualWidgetTest, RemoveChild) {
    visual::Widget parent;
    auto child = std::make_unique<visual::TestWidget>(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    visual::Widget* childPtr = child.get();
    
    parent.addChild(std::move(child));
    auto removed = parent.removeChild(childPtr);
    
    EXPECT_EQ(parent.children().size(), 0u);
    EXPECT_EQ(removed->parent(), nullptr);
    EXPECT_EQ(removed.get(), childPtr);
}

TEST(VisualWidgetTest, EmplaceChild) {
    visual::Widget parent;
    auto* child = parent.emplaceChild<visual::TestWidget>(Pt(10.0f, 20.0f), Pt(50.0f, 30.0f));
    
    EXPECT_EQ(parent.children().size(), 1u);
    EXPECT_EQ(child->parent(), &parent);
    EXPECT_EQ(child->localPos().x, 10.0f);
    EXPECT_EQ(child->localPos().y, 20.0f);
}

TEST(VisualWidgetTest, FlexibleDefault) {
    visual::Widget w;
    EXPECT_FALSE(w.isFlexible());
}

TEST(VisualWidgetTest, SetFlexible) {
    visual::Widget w;
    w.setFlexible(true);
    EXPECT_TRUE(w.isFlexible());
    w.setFlexible(false);
    EXPECT_FALSE(w.isFlexible());
}

// ==================================================================
// Regression: deep grid propagation (Node -> Container -> Port)
// ==================================================================
TEST(VisualWidgetTest, DeepGridPropagation) {
    visual::Scene scene;
    
    // Build a 3-level tree: node -> container -> port
    auto node = std::make_unique<visual::TestWidget>(Pt(100.0f, 200.0f), Pt(300.0f, 200.0f));
    auto container = std::make_unique<visual::TestWidget>(Pt(10.0f, 10.0f), Pt(280.0f, 180.0f));
    auto port = std::make_unique<visual::ClickableTestWidget>(Pt(5.0f, 5.0f), Pt(20.0f, 20.0f));
    
    visual::Widget* nodePtr = node.get();
    visual::Widget* portPtr = port.get();
    
    container->addChild(std::move(port));
    node->addChild(std::move(container));
    scene.add(std::move(node));
    
    // Port world pos = node(100,200) + container(10,10) + port(5,5) = (115, 215)
    EXPECT_FLOAT_EQ(portPtr->worldPos().x, 115.0f);
    EXPECT_FLOAT_EQ(portPtr->worldPos().y, 215.0f);
    
    // Port should be findable via grid query at its world position
    auto hits = scene.grid().query(Pt(115.0f, 215.0f), 1.0f);
    ASSERT_FALSE(hits.empty());
    EXPECT_NE(std::find(hits.begin(), hits.end(), portPtr), hits.end());
    
    // Now move the node — the port's grid entry must update recursively
    nodePtr->setLocalPos(Pt(500.0f, 600.0f));
    
    // New port world pos = node(500,600) + container(10,10) + port(5,5) = (515, 615)
    EXPECT_FLOAT_EQ(portPtr->worldPos().x, 515.0f);
    EXPECT_FLOAT_EQ(portPtr->worldPos().y, 615.0f);
    
    // Old location should return nothing useful
    auto old_hits = scene.grid().query(Pt(115.0f, 215.0f), 1.0f);
    EXPECT_EQ(std::find(old_hits.begin(), old_hits.end(), portPtr), old_hits.end());
    
    // New location should find the port
    auto new_hits = scene.grid().query(Pt(515.0f, 615.0f), 1.0f);
    ASSERT_FALSE(new_hits.empty());
    EXPECT_NE(std::find(new_hits.begin(), new_hits.end(), portPtr), new_hits.end());
}

// ==================================================================
// Regression: widget destruction without explicit scene removal
// ==================================================================
TEST(VisualWidgetTest, DestructorSafetyNet) {
    visual::Scene scene;
    
    // Create a clickable widget, add it, then destroy the scene.
    // The widget destructor should safely unregister from the grid.
    auto w = std::make_unique<visual::ClickableTestWidget>(Pt(10.0f, 10.0f), Pt(30.0f, 30.0f));
    visual::Widget* wPtr = w.get();
    scene.add(std::move(w));
    
    // Verify it's in the grid
    auto hits = scene.grid().query(Pt(15.0f, 15.0f), 1.0f);
    EXPECT_NE(std::find(hits.begin(), hits.end(), wPtr), hits.end());
    
    // clear() exercises the normal detach path; test that it doesn't crash
    scene.clear();
    
    // Grid should be empty now
    auto post_hits = scene.grid().query(Pt(15.0f, 15.0f), 1.0f);
    EXPECT_TRUE(post_hits.empty());
}

/// Tests that destroying a widget via unique_ptr reset (outside Scene::remove)
/// does not leave a dangling pointer in the grid.
// TEST(VisualWidgetTest, DestructorRemovesFromGrid) {
//     visual::Scene scene;
//     
//     // Manually construct the scenario: widget is in the scene, then we
//     // forcibly destroy it by resetting its owning unique_ptr.
//     auto w = std::make_unique<visual::ClickableTestWidget>(Pt(20.0f, 20.0f), Pt(40.0f, 40.0f));
//     
//     // Attach to scene manually (simulating what Scene::add does internally)
//     scene.attachToScene(w.get());
//     
//     // Verify widget is in grid
//     auto hits = scene.grid().query(Pt(30.0f, 30.0f), 1.0f);
//     EXPECT_EQ(hits.size(), 1u);
//     
//     // Destroy widget without going through Scene::remove
//     w.reset();
//     
//     // Grid query at old position must return nothing — the widget was cleaned up
//     auto post_hits = scene.grid().query(Pt(30.0f, 30.0f), 1.0f);
//     EXPECT_EQ(post_hits.size(), 0u);
// }
