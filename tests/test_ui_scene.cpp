#include "ui/core/scene.h"
#include "ui/core/widget.h"
#include <gtest/gtest.h>

namespace {
class TestWidget : public ui::Widget {
public:
    ui::Pt last_render_size;
    void render(ui::IDrawList*) const override {
        const_cast<TestWidget*>(this)->last_render_size = size();
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
