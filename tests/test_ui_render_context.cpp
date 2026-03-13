#include "ui/renderer/render_context.h"
#include <gtest/gtest.h>

TEST(UIRenderContext, DefaultValues) {
    ui::RenderContext ctx;
    
    EXPECT_FLOAT_EQ(ctx.zoom, 1.0f);
    EXPECT_FLOAT_EQ(ctx.pan.x, 0.0f);
    EXPECT_FLOAT_EQ(ctx.pan.y, 0.0f);
    EXPECT_EQ(ctx.hovered_id, 0);
    EXPECT_EQ(ctx.selected_id, 0);
    EXPECT_FALSE(ctx.is_dragging);
}

TEST(UIRenderContext, WorldToScreen) {
    ui::RenderContext ctx;
    ctx.zoom = 2.0f;
    ctx.pan = ui::Pt{100, 100};
    ctx.canvas_min = ui::Pt{50, 50};
    
    auto screen = ctx.world_to_screen(ui::Pt{200, 200});
    EXPECT_FLOAT_EQ(screen.x, 250.0f);
    EXPECT_FLOAT_EQ(screen.y, 250.0f);
}

TEST(UIRenderContext, SelectionIds) {
    ui::RenderContext ctx;
    ctx.selected_id = 42;
    ctx.hovered_id = 100;
    
    EXPECT_EQ(ctx.selected_id, 42u);
    EXPECT_EQ(ctx.hovered_id, 100u);
}

TEST(UIRenderContext, NoDomainPointers) {
    ui::RenderContext ctx;
    
    // These should work:
    ctx.dt = 0.016f;
    ctx.mouse_pos = ui::Pt{10, 20};
    ctx.is_dragging = true;
    
    EXPECT_FLOAT_EQ(ctx.dt, 0.016f);
    EXPECT_FLOAT_EQ(ctx.mouse_pos.x, 10.0f);
    EXPECT_TRUE(ctx.is_dragging);
}
