#include "ui/core/grid.h"
#include "ui/core/widget.h"
#include <gtest/gtest.h>

namespace {
class ClickableWidget : public ui::Widget {
public:
    bool isClickable() const override { return true; }
};
}

TEST(UIGrid, InsertAndQuery) {
    ui::Grid grid;
    
    auto w = std::make_unique<ClickableWidget>();
    w->setLocalPos(ui::Pt{0, 0});
    w->setSize(ui::Pt{100, 100});
    auto* ptr = w.get();
    
    grid.insert(ptr);
    
    auto results = grid.query(ui::Pt{50, 50}, 0);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], ptr);
}

TEST(UIGrid, Remove) {
    ui::Grid grid;
    
    auto w = std::make_unique<ClickableWidget>();
    w->setLocalPos(ui::Pt{0, 0});
    w->setSize(ui::Pt{100, 100});
    auto* ptr = w.get();
    
    grid.insert(ptr);
    grid.remove(ptr);
    
    auto results = grid.query(ui::Pt{50, 50}, 0);
    EXPECT_EQ(results.size(), 0u);
}

TEST(UIGrid, QueryWithMargin) {
    ui::Grid grid;
    
    auto w = std::make_unique<ClickableWidget>();
    w->setLocalPos(ui::Pt{0, 0});
    w->setSize(ui::Pt{100, 100});
    auto* ptr = w.get();
    
    grid.insert(ptr);
    
    auto results = grid.query(ui::Pt{150, 50}, 60);
    EXPECT_EQ(results.size(), 1u);
}

TEST(UIGrid, Clear) {
    ui::Grid grid;
    
    auto w1 = std::make_unique<ClickableWidget>();
    w1->setLocalPos(ui::Pt{0, 0});
    w1->setSize(ui::Pt{100, 100});
    grid.insert(w1.get());
    
    auto w2 = std::make_unique<ClickableWidget>();
    w2->setLocalPos(ui::Pt{200, 200});
    w2->setSize(ui::Pt{100, 100});
    grid.insert(w2.get());
    
    grid.clear();
    
    auto results = grid.query(ui::Pt{50, 50}, 0);
    EXPECT_EQ(results.size(), 0u);
}
