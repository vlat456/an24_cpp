#include "ui/layout/edges.h"
#include <gtest/gtest.h>

TEST(UILayout, EdgesExists) {
    ui::Edges e{5.0f, 10.0f, 5.0f, 10.0f};
    EXPECT_FLOAT_EQ(e.left, 5.0f);
    EXPECT_FLOAT_EQ(e.top, 10.0f);
    EXPECT_FLOAT_EQ(e.right, 5.0f);
    EXPECT_FLOAT_EQ(e.bottom, 10.0f);
}

TEST(UILayout, EdgesAll) {
    auto e = ui::Edges::all(8.0f);
    EXPECT_FLOAT_EQ(e.left, 8.0f);
    EXPECT_FLOAT_EQ(e.top, 8.0f);
    EXPECT_FLOAT_EQ(e.right, 8.0f);
    EXPECT_FLOAT_EQ(e.bottom, 8.0f);
}

TEST(UILayout, EdgesSymmetric) {
    auto e = ui::Edges::symmetric(10.0f, 5.0f);
    EXPECT_FLOAT_EQ(e.left, 10.0f);
    EXPECT_FLOAT_EQ(e.right, 10.0f);
    EXPECT_FLOAT_EQ(e.top, 5.0f);
    EXPECT_FLOAT_EQ(e.bottom, 5.0f);
}
