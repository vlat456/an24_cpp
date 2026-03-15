#include "ui/math/pt.h"
#include <gtest/gtest.h>

TEST(UIMath, PtExists) {
    ui::Pt p{10.0f, 20.0f};
    EXPECT_FLOAT_EQ(p.x, 10.0f);
    EXPECT_FLOAT_EQ(p.y, 20.0f);
}

TEST(UIMath, PtOperations) {
    ui::Pt a{5.0f, 10.0f};
    ui::Pt b{3.0f, 2.0f};
    
    auto sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 8.0f);
    EXPECT_FLOAT_EQ(sum.y, 12.0f);
    
    auto diff = a - b;
    EXPECT_FLOAT_EQ(diff.x, 2.0f);
    EXPECT_FLOAT_EQ(diff.y, 8.0f);
    
    auto scaled = a * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x, 10.0f);
    EXPECT_FLOAT_EQ(scaled.y, 20.0f);
}

TEST(UIMath, PtEquality) {
    ui::Pt a{1.0f, 2.0f};
    ui::Pt b{1.0f, 2.0f};
    ui::Pt c{1.0f, 3.0f};
    
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}
