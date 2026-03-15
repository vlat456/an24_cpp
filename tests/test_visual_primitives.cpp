#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "editor/visual/primitives/primitives.h"
#include "editor/visual/container/linear_layout.h"

namespace visual {

} // namespace visual

TEST(LabelTest, PreferredSizeWithDl) {
    visual::Label label("Hello", 12.0f);
    
    // Just check it doesn't crash - we can't easily mock IDrawList in this test
    Pt ps = label.preferredSize(nullptr);
    EXPECT_GT(ps.x, 0);
    EXPECT_EQ(ps.y, 12.0f);
}

TEST(LabelTest, SetSizeFromConstructor) {
    visual::Label label("Test", 14.0f);
    Pt ps = label.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, 4 * 14 * 0.6f); // "Test" = 4 chars
    EXPECT_EQ(ps.y, 14.0f);
}

TEST(SpacerTest, IsFlexible) {
    visual::Spacer spacer;
    EXPECT_TRUE(spacer.isFlexible());
}

TEST(SpacerTest, PreferredSizeIsZero) {
    visual::Spacer spacer;
    Pt ps = spacer.preferredSize(nullptr);
    EXPECT_EQ(ps.x, 0);
    EXPECT_EQ(ps.y, 0);
}

TEST(CircleTest, SizeFromRadius) {
    visual::Circle circle(10.0f, 0xFF0000FF);
    Pt sz = circle.size();
    
    EXPECT_EQ(sz.x, 20.0f); // radius * 2
    EXPECT_EQ(sz.y, 20.0f);
}

TEST(CircleTest, PreferredSize) {
    visual::Circle circle(25.0f, 0xFF0000FF);
    Pt ps = circle.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, 50.0f);
    EXPECT_EQ(ps.y, 50.0f);
}

TEST(LabelTest, WorldPos) {
    visual::Label label("Test", 12.0f);
    label.setLocalPos(Pt(100, 200));
    
    EXPECT_EQ(label.worldPos().x, 100);
    EXPECT_EQ(label.worldPos().y, 200);
}

TEST(LabelTest, ChildOfRow) {
    visual::Row row;
    auto* label = row.emplaceChild<visual::Label>("Hello", 12.0f);
    row.setLocalPos(Pt(50, 60));
    row.layout(100, 20);
    
    EXPECT_EQ(label->worldPos().x, 50);
    EXPECT_EQ(label->worldPos().y, 60);
}

TEST(CircleTest, WorldPos) {
    visual::Circle circle(15.0f, 0xFF00FF00);
    circle.setLocalPos(Pt(10, 20));
    
    EXPECT_EQ(circle.worldPos().x, 10);
    EXPECT_EQ(circle.worldPos().y, 20);
}
