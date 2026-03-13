#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "ui/core/grid.h"
#include "editor/visual/scene.h"
#include "editor/visual/render_context.h"

using ui::Pt;

namespace visual {

class ClickableWidget : public Widget {
public:
    ClickableWidget(std::string id, Pt pos, Pt sz) : id_(std::move(id)) {
        local_pos_ = pos;
        size_ = sz;
    }
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    void render(IDrawList*, const RenderContext&) const override {}

private:
    std::string id_;
};

} // namespace visual

TEST(SceneTest, AddWidget) {
    visual::Scene scene;
    auto w = std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    scene.add(std::move(w));
    
    EXPECT_EQ(scene.roots().size(), 1u);
    EXPECT_EQ(scene.find("widget1")->scene(), &scene);
}

TEST(SceneTest, FindWidget) {
    visual::Scene scene;
    scene.add(std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f)));
    scene.add(std::make_unique<visual::ClickableWidget>("widget2", Pt(200.0f, 200.0f), Pt(50.0f, 50.0f)));
    
    auto* found = scene.find("widget1");
    EXPECT_NE(found, nullptr);
    
    auto* notFound = scene.find("nonexistent");
    EXPECT_EQ(notFound, nullptr);
}

TEST(SceneTest, RemoveWidget) {
    visual::Scene scene;
    auto w = std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    scene.add(std::move(w));
    
    auto* wptr = scene.find("widget1");
    scene.remove(wptr);
    scene.flushRemovals();
    
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(SceneTest, WidgetPropagatesToChildren) {
    visual::Scene scene;
    auto parent = std::make_unique<visual::ClickableWidget>("parent", Pt(100.0f, 100.0f), Pt(100.0f, 100.0f));
    auto child = std::make_unique<visual::ClickableWidget>("child", Pt(10.0f, 10.0f), Pt(50.0f, 50.0f));
    
    parent->addChild(std::move(child));
    scene.add(std::move(parent));
    
    auto* p = scene.find("parent");
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(p->scene(), &scene);
    EXPECT_EQ(p->children().size(), 1u);
    EXPECT_EQ(static_cast<visual::Widget*>(p->children()[0].get())->scene(), &scene);
}

TEST(SceneTest, GridTracksClickableWidgets) {
    visual::Scene scene;
    scene.add(std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f)));
    
    auto result = scene.grid().query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_EQ(result.size(), 1u);
}

TEST(SceneTest, RemoveFromGridOnDelete) {
    visual::Scene scene;
    auto w = std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    auto* wptr = w.get();
    scene.add(std::move(w));
    
    scene.remove(wptr);
    scene.flushRemovals();
    
    auto result = scene.grid().query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_TRUE(result.empty());
}

TEST(SceneTest, RenderCallsWidgetRender) {
    visual::Scene scene;
    scene.add(std::make_unique<visual::ClickableWidget>("widget1", Pt(100.0f, 100.0f), Pt(50.0f, 50.0f)));
    
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    EXPECT_NO_FATAL_FAILURE(scene.render(nullptr, ctx));
}
