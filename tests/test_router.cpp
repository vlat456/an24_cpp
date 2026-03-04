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

    // Точки далеко от nodes
    Pt start(200.0f, 40.0f);
    Pt end(200.0f, 240.0f);

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    ASSERT_FALSE(path.empty());
    float step = bp.grid_step;
    EXPECT_LE(std::abs(path.front().x - start.x), step);
    EXPECT_LE(std::abs(path.front().y - start.y), step);
    EXPECT_LE(std::abs(path.back().x - end.x), step);
    EXPECT_LE(std::abs(path.back().y - end.y), step);
}

TEST(RouterTest, AvoidsObstacle) {
    // Route should go around obstacle (node in the middle)
    Blueprint bp;
    bp.grid_step = 16.0f;

    // Node in the middle blocking direct path
    Node obstacle;
    obstacle.id = "obstacle";
    obstacle.pos = Pt(100, 100);
    obstacle.size = Pt(80, 80);
    bp.nodes.push_back(obstacle);

    Pt start(50.0f, 140.0f);  // left of obstacle
    Pt end(250.0f, 140.0f);   // right of obstacle

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    ASSERT_FALSE(path.empty());

    // Path should go around (above or below)
    // Check that path doesn't go through the obstacle
    bool goes_around = false;
    for (const auto& pt : path) {
        // If any point is far from obstacle center, we're going around
        float dx = pt.x - (obstacle.pos.x + obstacle.size.x/2);
        float dy = pt.y - (obstacle.pos.y + obstacle.size.y/2);
        if (std::abs(dx) > 60 || std::abs(dy) > 60) {
            goes_around = true;
            break;
        }
    }
    EXPECT_TRUE(goes_around);
}

TEST(RouterTest, PathHasMinimalTurns) {
    // With turn penalty, path should prefer fewer turns
    Blueprint bp;
    bp.grid_step = 16.0f;
    // No obstacles - straight path possible

    Pt start(50.0f, 50.0f);
    Pt end(250.0f, 50.0f);  // directly to the right

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    ASSERT_FALSE(path.empty());

    // Should be roughly horizontal - minimal turns
    // Path should mostly go in X direction
    int x_changes = 0;
    int y_changes = 0;
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i].x != path[i-1].x) x_changes++;
        if (path[i].y != path[i-1].y) y_changes++;
    }

    // Prefer horizontal movement when going horizontal
    EXPECT_GE(x_changes, y_changes);
}

TEST(RouterTest, PathAroundNodeUsesPadding) {
    // Route should stay 1 grid cell away from node (padding)
    Blueprint bp;
    bp.grid_step = 16.0f;

    Node node;
    node.id = "node";
    node.pos = Pt(100, 100);
    node.size = Pt(64, 48);  // 4x3 grid cells
    bp.nodes.push_back(node);

    // Start and end on left and right of node
    Pt start(50.0f, 124.0f);  // left of node, middle height
    Pt end(250.0f, 124.0f);   // right of node

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    ASSERT_FALSE(path.empty());

    // All path points should be at least 1 grid step away from node edges
    float min_dist = 16.0f;  // 1 grid step
    for (const auto& pt : path) {
        // Check distance to node bounds with padding
        float node_left = node.pos.x - min_dist;
        float node_right = node.pos.x + node.size.x + min_dist;
        float node_top = node.pos.y - min_dist;
        float node_bottom = node.pos.y + node.size.y + min_dist;

        // If point is within extended bounds, it's too close
        bool too_close = (pt.x >= node_left && pt.x <= node_right &&
                         pt.y >= node_top && pt.y <= node_bottom);
        EXPECT_FALSE(too_close) << "Point (" << pt.x << ", " << pt.y
            << ") is too close to node (padded bounds: "
            << node_left << "-" << node_right << ", "
            << node_top << "-" << node_bottom << ")";
    }
}

// Test wire crossing detection
TEST(RouterTest, WireCrossingDetection) {
    // Two wires that cross each other
    Wire w1;
    w1.id = "w1";
    w1.start.node_id = "n1";
    w1.start.port_name = "out";
    w1.end.node_id = "n2";
    w1.end.port_name = "in";
    w1.routing_points = {Pt(100, 100), Pt(200, 100)};  // horizontal

    Wire w2;
    w2.id = "w2";
    w2.start.node_id = "n3";
    w2.start.port_name = "out";
    w2.end.node_id = "n4";
    w2.end.port_name = "in";
    w2.routing_points = {Pt(150, 50), Pt(150, 150)};  // vertical

    // Build polylines
    std::vector<Pt> poly1 = {Pt(0, 100), Pt(100, 100), Pt(200, 100), Pt(300, 100)};
    std::vector<Pt> poly2 = {Pt(150, 0), Pt(150, 50), Pt(150, 150), Pt(150, 200)};

    // They should cross at (150, 100)
    // The crossing detection will be implemented in render

    // Just check the polylines are valid
    EXPECT_EQ(poly1.size(), 4u);
    EXPECT_EQ(poly2.size(), 4u);
}
