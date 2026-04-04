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
    EXPECT_EQ(results[0], (ui::Widget*)ptr);
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

// ============================================================
// REGRESSION: Fix 5 — Empty cell cleanup on remove
// ============================================================
// Before this fix, removing the last widget from a grid cell would
// leave an empty Cell entry in the hash map, leaking memory over time.
// The fix erases the cell when its widget vector becomes empty.

TEST(UIGrid, REGRESSION_RemoveCleanupEmptyCells) {
    ui::Grid grid;
    
    // Insert a small widget that fits in a single cell
    auto w = std::make_unique<ClickableWidget>();
    w->setLocalPos(ui::Pt{10, 10});
    w->setSize(ui::Pt{20, 20});
    grid.insert(w.get());
    
    // Verify it's queryable
    auto results = grid.query(ui::Pt{20, 20}, 0);
    EXPECT_EQ(results.size(), 1u);
    
    // Remove it
    grid.remove(w.get());
    
    // Verify the grid has no entries at all — forEachCell should not call fn
    int cell_count = 0;
    grid.forEachCell([&](const std::vector<ui::Widget*>& widgets) {
        cell_count++;
    });
    EXPECT_EQ(cell_count, 0) << "Empty cells should be erased from the hash map on remove";
}

TEST(UIGrid, REGRESSION_RemoveOneOfTwoInSameCell) {
    ui::Grid grid;
    
    // Two widgets overlapping the same cell
    auto w1 = std::make_unique<ClickableWidget>();
    w1->setLocalPos(ui::Pt{10, 10});
    w1->setSize(ui::Pt{20, 20});
    grid.insert(w1.get());
    
    auto w2 = std::make_unique<ClickableWidget>();
    w2->setLocalPos(ui::Pt{15, 15});
    w2->setSize(ui::Pt{20, 20});
    grid.insert(w2.get());
    
    // Remove w1 — cell should still exist with w2
    grid.remove(w1.get());
    
    auto results = grid.query(ui::Pt{25, 25}, 0);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], (ui::Widget*)w2.get());
    
    // Remove w2 — cell should be cleaned up
    grid.remove(w2.get());
    
    int cell_count = 0;
    grid.forEachCell([&](const std::vector<ui::Widget*>&) { cell_count++; });
    EXPECT_EQ(cell_count, 0);
}

// ============================================================
// REGRESSION: Grid update moves widget correctly
// ============================================================

TEST(UIGrid, UpdateMovesWidget) {
    ui::Grid grid;
    
    // Use a small widget (10x10) that fits in a single 64x64 cell
    auto w = std::make_unique<ClickableWidget>();
    w->setLocalPos(ui::Pt{10, 10});
    w->setSize(ui::Pt{10, 10});
    grid.insert(w.get());
    
    // Widget is queryable at original position
    auto r1 = grid.query(ui::Pt{15, 15}, 0);
    EXPECT_EQ(r1.size(), 1u);
    
    // Move widget far away (still fits in a single cell)
    w->setLocalPos(ui::Pt{500, 500});
    grid.update(w.get());
    
    // No longer queryable at old position
    auto r2 = grid.query(ui::Pt{15, 15}, 0);
    EXPECT_EQ(r2.size(), 0u);
    
    // Queryable at new position
    auto r3 = grid.query(ui::Pt{505, 505}, 0);
    EXPECT_EQ(r3.size(), 1u);
    EXPECT_EQ(r3[0], (ui::Widget*)w.get());
    
    // Old cells should be cleaned up — only 1 cell for the new position
    int cell_count = 0;
    grid.forEachCell([&](const std::vector<ui::Widget*>&) { cell_count++; });
    EXPECT_EQ(cell_count, 1) << "After update, only the new cell should exist";
}

