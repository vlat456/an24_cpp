// TDD Tests for A* Router
// Тестируем router TDD - Red->Green

#include <gtest/gtest.h>
#include <cmath>
#include "editor/router/router.h"
#include "editor/data/blueprint.h"

TEST(RouterTest, GridConversion) {
    // round trip: world -> grid -> world
    Pt world(50.0f, 75.0f);
    float step = 16.0f;

    GridPt grid = grid_from_world(world, step);
    EXPECT_EQ(grid.x, 3); // 50/16 = 3.125 -> 3
    EXPECT_EQ(grid.y, 5);  // 75/16 = 4.6875 -> 5

    Pt world2 = grid_to_world(grid, step);
    EXPECT_NEAR(world2.x, 48.0f, 0.1f); // 3 * 16
    EXPECT_NEAR(world2.y, 80.0f, 0.1f); // 5 * 16
}

TEST(RouterTest, EmptySpacePath) {
    // A* should find path in empty space
    // Используем больший clearance=2 чтобы дать место для роутинга
    Blueprint bp;
    bp.grid_step = 16.0f;

    Node n1;
    n1.id = "n1";
    n1.pos = Pt(0, 0);
    n1.size = Pt(100, 80);
    bp.nodes.push_back(n1);

    Node n2;
    n2.id = "n2";
    n2.pos = Pt(300, 200);
    n2.size = Pt(100, 80);
    bp.nodes.push_back(n2);

    // Точки далеко от nodes - чтобы не были внутри clearance зоны
    Pt start(200.0f, 40.0f);  // между nodes, справа от n1
    Pt end(200.0f, 240.0f);   // между nodes, справа от n2

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    // Should find a path
    ASSERT_FALSE(path.empty());

    // First and last points should be near start/end (within grid cell)
    // But not exactly equal due to grid snapping
    float step = bp.grid_step;
    EXPECT_LE(std::abs(path.front().x - start.x), step);
    EXPECT_LE(std::abs(path.front().y - start.y), step);
    EXPECT_LE(std::abs(path.back().x - end.x), step);
    EXPECT_LE(std::abs(path.back().y - end.y), step);
}
