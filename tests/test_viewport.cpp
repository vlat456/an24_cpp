#include <gtest/gtest.h>
#include "editor/viewport/viewport.h"
#include "editor/data/pt.h"

/// TDD Step 3: Viewport - сначала тесты

TEST(ViewportTest, DefaultValues) {
    Viewport vp;
    EXPECT_EQ(vp.pan.x, 0.0f);
    EXPECT_EQ(vp.pan.y, 0.0f);
    EXPECT_EQ(vp.zoom, 1.0f);
    EXPECT_EQ(vp.grid_step, 16.0f);  // [BUG-e5f6] unified with Blueprint default
}

TEST(ViewportTest, ScreenToWorld_Identity) {
    Viewport vp;
    // При zoom=1, pan=0 - экранные и мировые координаты совпадают
    Pt screen(100.0f, 200.0f);
    Pt canvas_min(0.0f, 0.0f);
    Pt world = vp.screen_to_world(screen, canvas_min);
    EXPECT_EQ(world.x, 100.0f);
    EXPECT_EQ(world.y, 200.0f);
}

TEST(ViewportTest, WorldToScreen_Identity) {
    Viewport vp;
    Pt world(100.0f, 200.0f);
    Pt canvas_min(0.0f, 0.0f);
    Pt screen = vp.world_to_screen(world, canvas_min);
    EXPECT_EQ(screen.x, 100.0f);
    EXPECT_EQ(screen.y, 200.0f);
}

TEST(ViewportTest, ScreenToWorld_WithPan) {
    Viewport vp;
    vp.pan = Pt(50.0f, 100.0f); // сдвиг на 50, 100
    Pt screen(100.0f, 200.0f);
    Pt canvas_min(0.0f, 0.0f);
    Pt world = vp.screen_to_world(screen, canvas_min);
    // world = (screen - canvas_min) / zoom + pan
    EXPECT_NEAR(world.x, 150.0f, 0.01f);
    EXPECT_NEAR(world.y, 300.0f, 0.01f);
}

TEST(ViewportTest, WorldToScreen_WithPan) {
    Viewport vp;
    vp.pan = Pt(50.0f, 100.0f);
    Pt world(150.0f, 300.0f);
    Pt canvas_min(0.0f, 0.0f);
    Pt screen = vp.world_to_screen(world, canvas_min);
    EXPECT_NEAR(screen.x, 100.0f, 0.01f);
    EXPECT_NEAR(screen.y, 200.0f, 0.01f);
}

TEST(ViewportTest, ScreenToWorld_WithZoom) {
    Viewport vp;
    vp.zoom = 2.0f;
    Pt screen(100.0f, 100.0f);
    Pt canvas_min(0.0f, 0.0f);
    Pt world = vp.screen_to_world(screen, canvas_min);
    // world = screen / zoom = 50
    EXPECT_NEAR(world.x, 50.0f, 0.01f);
    EXPECT_NEAR(world.y, 50.0f, 0.01f);
}

TEST(ViewportTest, PanBy) {
    Viewport vp;
    vp.pan_by(Pt(100.0f, 50.0f)); // сдвиг на 100, 50 экранных пикселей
    // При zoom=1, pan уменьшается (экран движется в противоположную сторону)
    EXPECT_NEAR(vp.pan.x, -100.0f, 0.01f);
    EXPECT_NEAR(vp.pan.y, -50.0f, 0.01f);
}

TEST(ViewportTest, ZoomAt) {
    Viewport vp;
    float old_zoom = vp.zoom;
    vp.zoom_at(0.1f, Pt(100.0f, 100.0f), Pt(0.0f, 0.0f)); // zoom in
    EXPECT_GT(vp.zoom, old_zoom);
}

TEST(ViewportTest, ZoomClamped) {
    Viewport vp;
    // Zoom in много раз
    for (int i = 0; i < 100; i++) {
        vp.zoom_at(1.0f, Pt(100.0f, 100.0f), Pt(0.0f, 0.0f));
    }
    // Должен быть огранилен
    EXPECT_LE(vp.zoom, 4.0f);

    // Zoom out много раз
    for (int i = 0; i < 100; i++) {
        vp.zoom_at(-1.0f, Pt(100.0f, 100.0f), Pt(0.0f, 0.0f));
    }
    // Должен быть огранилен
    EXPECT_GE(vp.zoom, 0.25f);
}
