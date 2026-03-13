#include "ui/core/scene.h"
#include "ui/core/widget.h"
#include <gtest/gtest.h>

namespace {
class TestWidget : public ui::Widget {
public:
    ui::Pt last_render_size;
    void render(ui::IDrawList*) const override {
        const_cast<TestWidget*>(this)->last_render_size = size_;
    }
};
}

TEST(UIScene, AddWidget) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    w->setSize(ui::Pt{100, 50});
    auto* ptr = w.get();
    
    scene.add(std::move(w));
    
    EXPECT_EQ(scene.roots().size(), 1u);
    EXPECT_EQ(scene.roots()[0].get(), ptr);
}

TEST(UIScene, RemoveWidget) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    auto* ptr = w.get();
    scene.add(std::move(w));
    
    scene.remove(ptr);
    scene.flushRemovals();
    
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, Clear) {
    ui::Scene scene;
    scene.add(std::make_unique<TestWidget>());
    scene.add(std::make_unique<TestWidget>());
    
    scene.clear();
    
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, FindById) {
    ui::Scene scene;
    
    class NamedWidget : public ui::Widget {
    public:
        std::string id_;
        NamedWidget(std::string_view id) : id_(id) {}
        std::string_view id() const override { return id_; }
    };
    
    scene.add(std::make_unique<NamedWidget>("widget_a"));
    scene.add(std::make_unique<NamedWidget>("widget_b"));
    
    EXPECT_NE(scene.find("widget_a"), nullptr);
    EXPECT_NE(scene.find("widget_b"), nullptr);
    EXPECT_EQ(scene.find("widget_c"), nullptr);
}


TEST(UIScene, RenderWithoutRenderContext) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    w->setSize(ui::Pt{100, 50});
    scene.add(std::move(w));
    
    scene.render(nullptr);
    
    EXPECT_EQ(scene.roots()[0]->size().x, 100.0f);
}

// ============================================================================
// FlushGuard tests
// ============================================================================

TEST(UIScene, FlushGuard_AutoFlushOnDestruction) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    auto* ptr = w.get();
    scene.add(std::move(w));
    
    EXPECT_EQ(scene.roots().size(), 1u);
    
    {
        auto guard = scene.flushGuard();
        scene.remove(ptr);
        // Widget still present (deferred removal)
        EXPECT_EQ(scene.roots().size(), 1u);
    }
    // Guard destructor called flushRemovals()
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, FlushGuard_NoFlushWhenReleased) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    auto* ptr = w.get();
    scene.add(std::move(w));
    
    {
        auto guard = scene.flushGuard();
        scene.remove(ptr);
        guard.release();  // Cancel auto-flush
    }
    // Guard was released, so no flush happened
    EXPECT_EQ(scene.roots().size(), 1u);
    
    // Manual flush still works
    scene.flushRemovals();
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, FlushGuard_MoveSemantics) {
    ui::Scene scene;
    auto w = std::make_unique<TestWidget>();
    auto* ptr = w.get();
    scene.add(std::move(w));
    
    scene.remove(ptr);
    
    {
        auto guard1 = scene.flushGuard();
        auto guard2 = std::move(guard1);  // Move construct
        // guard1 is now null, guard2 owns the flush responsibility
    }
    // guard2's destructor flushed
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, FlushGuard_MultipleRemovals) {
    ui::Scene scene;
    auto* ptr1 = scene.add(std::make_unique<TestWidget>());
    auto* ptr2 = scene.add(std::make_unique<TestWidget>());
    auto* ptr3 = scene.add(std::make_unique<TestWidget>());
    
    EXPECT_EQ(scene.roots().size(), 3u);
    
    {
        auto guard = scene.flushGuard();
        scene.remove(ptr1);
        scene.remove(ptr2);
        scene.remove(ptr3);
    }
    
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(UIScene, FlushGuard_EarlyReturn) {
    ui::Scene scene;
    auto* ptr = scene.add(std::make_unique<TestWidget>());
    
    auto remove_and_return = [&]() -> bool {
        auto guard = scene.flushGuard();
        scene.remove(ptr);
        return true;
        // Guard flushes here even on early return
    };
    
    EXPECT_EQ(scene.roots().size(), 1u);
    bool result = remove_and_return();
    EXPECT_TRUE(result);
    EXPECT_EQ(scene.roots().size(), 0u);  // Flushed despite early return
}
