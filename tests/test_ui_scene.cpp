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

// ==========================================================================
// isIndexable
// ==========================================================================

namespace {
class NonIndexableChild : public ui::Widget {
public:
    explicit NonIndexableChild(std::string id) : id_(std::move(id)) {}
    std::string_view id() const override { return id_; }
    bool isIndexable() const override { return false; }
private:
    std::string id_;
};

class IndexableWidget : public ui::Widget {
public:
    explicit IndexableWidget(std::string id) : id_(std::move(id)) {}
    std::string_view id() const override { return id_; }
private:
    std::string id_;
};
} // namespace

// Non-indexable children should not shadow root widgets in id_index_.
TEST(UIScene, IsIndexable_ChildDoesNotShadowRoot) {
    ui::Scene scene;

    // Add a root widget with id "w1"
    auto root = std::make_unique<IndexableWidget>("w1");
    auto* root_ptr = root.get();

    // Give it a non-indexable child with the SAME id "w1"
    root->addChild(std::make_unique<NonIndexableChild>("w1"));

    scene.add(std::move(root));

    // scene.find("w1") should return the root, not the child
    EXPECT_EQ(scene.find("w1"), root_ptr);
}

// Non-indexable widget at root level should not be findable.
TEST(UIScene, IsIndexable_NonIndexableRootNotFindable) {
    ui::Scene scene;

    auto w = std::make_unique<NonIndexableChild>("hidden");
    scene.add(std::move(w));

    // The widget is in roots_ but should NOT be in id_index_
    EXPECT_EQ(scene.roots().size(), 1u);
    EXPECT_EQ(scene.find("hidden"), nullptr);
}

// Indexable children ARE findable (only non-indexable are skipped).
TEST(UIScene, IsIndexable_IndexableChildIsFindable) {
    ui::Scene scene;

    auto parent = std::make_unique<IndexableWidget>("parent");
    auto child = std::make_unique<IndexableWidget>("child");
    auto* child_ptr = child.get();
    parent->addChild(std::move(child));
    scene.add(std::move(parent));

    EXPECT_EQ(scene.find("child"), child_ptr);
}

// After removing a root that has non-indexable children,
// the id_index_ should not contain stale entries.
TEST(UIScene, IsIndexable_UnindexSkipsNonIndexable) {
    ui::Scene scene;

    auto root = std::make_unique<IndexableWidget>("w1");
    auto* root_ptr = root.get();
    root->addChild(std::make_unique<NonIndexableChild>("child_id"));
    scene.add(std::move(root));

    EXPECT_EQ(scene.find("w1"), root_ptr);

    scene.remove(root_ptr);
    scene.flushRemovals();

    // Both root and child should be gone from the index
    EXPECT_EQ(scene.find("w1"), nullptr);
    EXPECT_EQ(scene.find("child_id"), nullptr);
}

// attachToScene / detachFromScene respect isIndexable.
TEST(UIScene, IsIndexable_AttachDetachRespected) {
    ui::Scene scene;

    // Add a root widget first
    auto root = std::make_unique<IndexableWidget>("wire_3");
    auto* root_ptr = root.get();
    scene.add(std::move(root));
    EXPECT_EQ(scene.find("wire_3"), root_ptr);

    // Create a non-indexable widget with the same ID (simulates alias port)
    auto alias = std::make_unique<NonIndexableChild>("wire_3");
    auto* alias_ptr = alias.get();

    // Attach the alias — should NOT overwrite the root in id_index_
    scene.attachToScene(alias_ptr);
    EXPECT_EQ(scene.find("wire_3"), root_ptr);

    // Detach the alias — root should still be findable
    scene.detachFromScene(alias_ptr);
    EXPECT_EQ(scene.find("wire_3"), root_ptr);
}
