#include "ui/core/widget.h"
#include <gtest/gtest.h>

TEST(UIWidget, BasicGeometry) {
    ui::Widget w;
    w.setLocalPos(ui::Pt{10, 20});
    w.setSize(ui::Pt{100, 50});
    
    EXPECT_FLOAT_EQ(w.localPos().x, 10.0f);
    EXPECT_FLOAT_EQ(w.localPos().y, 20.0f);
    EXPECT_FLOAT_EQ(w.size().x, 100.0f);
    EXPECT_FLOAT_EQ(w.size().y, 50.0f);
}

TEST(UIWidget, WorldPos) {
    ui::Widget parent;
    parent.setLocalPos(ui::Pt{50, 50});
    
    parent.addChild(std::make_unique<ui::Widget>());
    parent.children()[0]->setLocalPos(ui::Pt{10, 10});
    
    // Parent world pos = local pos
    EXPECT_FLOAT_EQ(parent.worldPos().x, 50.0f);
}

TEST(UIWidget, Contains) {
    ui::Widget w;
    w.setLocalPos(ui::Pt{0, 0});
    w.setSize(ui::Pt{100, 100});
    
    EXPECT_TRUE(w.contains(ui::Pt{50, 50}));
    EXPECT_TRUE(w.contains(ui::Pt{0, 0}));
    EXPECT_TRUE(w.contains(ui::Pt{100, 100}));
    EXPECT_FALSE(w.contains(ui::Pt{101, 50}));
}

TEST(UIWidget, NumericZOrder) {
    ui::Widget w;
    w.setZOrder(1.5f);
    EXPECT_FLOAT_EQ(w.zOrder(), 1.5f);
    
    w.setZOrder(-0.5f);
    EXPECT_FLOAT_EQ(w.zOrder(), -0.5f);
}

TEST(UIWidget, DefaultZOrder) {
    ui::Widget w;
    EXPECT_FLOAT_EQ(w.zOrder(), 0.0f);
}
