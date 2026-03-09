// TDD Tests for A* Router
// Тестируем router TDD - Red->Green

#include <gtest/gtest.h>
#include <cmath>
#include "editor/router/router.h"
#include "editor/router/crossings.h"
#include "editor/data/blueprint.h"
#include "editor/visual/trigonometry.h"
#include "editor/visual/node/node.h"

// ============================================================================
// Grid conversion tests
// ============================================================================

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

TEST(RouterTest, GridConversionNegative) {
    float step = 16.0f;
    GridPt g = grid_from_world(Pt(-50.0f, -75.0f), step);
    EXPECT_EQ(g.x, -3);  // -50/16 = -3.125 -> -3
    EXPECT_EQ(g.y, -5);  // -75/16 = -4.6875 -> -5
}

TEST(RouterTest, GridConversionZero) {
    float step = 16.0f;
    GridPt g = grid_from_world(Pt(0.0f, 0.0f), step);
    EXPECT_EQ(g.x, 0);
    EXPECT_EQ(g.y, 0);
}

TEST(RouterTest, GridPtEquality) {
    GridPt a{3, 5};
    GridPt b{3, 5};
    GridPt c{3, 6};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(RouterTest, GridPtHashNoncollision) {
    // Verify hash function doesn't trivially collide for nearby points
    GridPtHash h;
    size_t h1 = h(GridPt{0, 1});
    size_t h2 = h(GridPt{1, 0});
    size_t h3 = h(GridPt{0, 0});
    EXPECT_NE(h1, h2);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h2, h3);
}

// ============================================================================
// A* core tests
// ============================================================================

TEST(RouterTest, EmptySpaceStraightLine) {
    // A* should find a straight path with no obstacles
    GridPt start{0, 0};
    GridPt goal{10, 0};
    std::unordered_set<GridPt, GridPtHash> obstacles;

    auto path = astar_search(start, goal, obstacles);

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->front().x, 0);
    EXPECT_EQ(path->front().y, 0);
    EXPECT_EQ(path->back().x, 10);
    EXPECT_EQ(path->back().y, 0);

    // All points should be on y=0 (straight horizontal)
    for (const auto& pt : *path) {
        EXPECT_EQ(pt.y, 0);
    }
}

TEST(RouterTest, AStarFindsPathAroundObstacle) {
    GridPt start{0, 0};
    GridPt goal{4, 0};
    std::unordered_set<GridPt, GridPtHash> obstacles;
    // Wall at x=2, y=-1..1
    obstacles.insert(GridPt{2, -1});
    obstacles.insert(GridPt{2, 0});
    obstacles.insert(GridPt{2, 1});

    auto path = astar_search(start, goal, obstacles);

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->front().x, 0);
    EXPECT_EQ(path->back().x, 4);

    // Path should not go through any obstacle
    for (const auto& pt : *path) {
        EXPECT_FALSE(obstacles.count(pt)) << "Path goes through obstacle at (" << pt.x << "," << pt.y << ")";
    }
}

TEST(RouterTest, AStarNoPath) {
    // Completely walled off
    GridPt start{0, 0};
    GridPt goal{4, 0};
    std::unordered_set<GridPt, GridPtHash> obstacles;
    // Surround start
    obstacles.insert(GridPt{1, 0});
    obstacles.insert(GridPt{-1, 0});
    obstacles.insert(GridPt{0, 1});
    obstacles.insert(GridPt{0, -1});

    auto path = astar_search(start, goal, obstacles);
    EXPECT_FALSE(path.has_value());
}

TEST(RouterTest, AStarSameStartAndGoal) {
    GridPt start{5, 5};
    std::unordered_set<GridPt, GridPtHash> obstacles;

    auto path = astar_search(start, start, obstacles);

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->size(), 1u);
    EXPECT_EQ(path->front().x, 5);
}

TEST(RouterTest, TurnPenaltyPrefersStraight) {
    // Path should prefer going straight even if it's longer in Manhattan distance
    // With turn penalty = 15, a path with 0 turns is preferred over a shorter
    // path with a turn if the difference is < 15 cells
    GridPt start{0, 0};
    GridPt goal{10, 0};  // straight horizontal
    std::unordered_set<GridPt, GridPtHash> obstacles;

    auto path = astar_search(start, goal, obstacles);

    ASSERT_TRUE(path.has_value());
    // Should be a perfect straight line (11 points, x=0..10, y=0)
    EXPECT_EQ(path->size(), 11u);
    for (const auto& pt : *path) {
        EXPECT_EQ(pt.y, 0) << "Path deviated from straight line at x=" << pt.x;
    }
}

// ============================================================================
// RoutingGrid tests
// ============================================================================

TEST(RouterTest, RoutingGridBasics) {
    RoutingGrid grid;
    GridPt pt{5, 5};

    EXPECT_EQ(grid.get(pt), CellEmpty);
    EXPECT_FALSE(grid.is_blocked(pt));

    grid.mark(pt, CellNode);
    EXPECT_TRUE(grid.is_blocked(pt));
    EXPECT_EQ(grid.get(pt), CellNode);
}

TEST(RouterTest, RoutingGridWireCrossingCost) {
    RoutingGrid grid;
    GridPt pt{5, 5};

    // Mark cell as having a vertical wire
    grid.mark(pt, CellWireV);

    // Crossing perpendicular (horiz entering vert wire) costs JUMP_OVER_COST
    EXPECT_NEAR(grid.wire_crossing_cost(pt, Dir::Horiz), JUMP_OVER_COST, 0.01f);

    // Parallel (vert entering vert wire) is very expensive
    EXPECT_GT(grid.wire_crossing_cost(pt, Dir::Vert), 50.0f);

    // Empty cell has no extra cost
    EXPECT_NEAR(grid.wire_crossing_cost(GridPt{0, 0}, Dir::Horiz), 0.0f, 0.01f);
}

TEST(RouterTest, RoutingGridFlagsOr) {
    RoutingGrid grid;
    GridPt pt{5, 5};

    grid.mark(pt, CellWireH);
    grid.mark(pt, CellWireV);

    // Both flags should be set
    EXPECT_EQ(grid.get(pt), CellWireH | CellWireV);
}

// ============================================================================
// Obstacle building
// ============================================================================

TEST(RouterTest, MakeObstaclesPadding) {
    std::vector<Node> nodes;
    Node n;
    n.id = "n1";
    n.pos = Pt(32, 32);    // grid (2, 2)
    n.size = Pt(32, 32);   // grid (2, 2) -> max at grid (4, 4)
    nodes.push_back(n);

    float step = 16.0f;
    auto obs = make_obstacles(nodes, step, 1);

    // With clearance=1: min=(2-1,2-1)=(1,1), max=(4+1,4+1)=(5,5)
    EXPECT_TRUE(obs.count(GridPt{1, 1}));  // corner with padding
    EXPECT_TRUE(obs.count(GridPt{5, 5}));  // corner with padding
    EXPECT_TRUE(obs.count(GridPt{3, 3}));  // inside node

    // Outside padding
    EXPECT_FALSE(obs.count(GridPt{0, 0}));
    EXPECT_FALSE(obs.count(GridPt{6, 6}));
}

TEST(RouterTest, MakeRoutingGridWithWires) {
    std::vector<Node> nodes;
    float step = 16.0f;

    // A horizontal wire from (0,0) to (80, 0) -> grid (0,0) to (5, 0)
    std::vector<std::vector<Pt>> existing_wires = {
        {Pt(0, 0), Pt(80, 0)}
    };

    auto grid = make_routing_grid(nodes, existing_wires, step, 1);

    // Cells along the wire should have CellWireH
    EXPECT_TRUE(grid.get(GridPt{0, 0}) & CellWireH);
    EXPECT_TRUE(grid.get(GridPt{3, 0}) & CellWireH);
    EXPECT_TRUE(grid.get(GridPt{5, 0}) & CellWireH);

    // Adjacent cells (wire padding) should also have CellWireH
    EXPECT_TRUE(grid.get(GridPt{3, 1}) & CellWireH);
    EXPECT_TRUE(grid.get(GridPt{3, -1}) & CellWireH);
}

// ============================================================================
// High-level routing
// ============================================================================

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

    // Start and end on left and right of node (outside obstacle zone)
    Pt start(32.0f, 124.0f);
    Pt end(250.0f, 124.0f);

    auto path = route_around_nodes(start, end, bp.nodes, bp.grid_step);

    ASSERT_FALSE(path.empty());

    // Intermediate path points (exclude first and last — those are original port positions)
    for (size_t i = 1; i + 1 < path.size(); i++) {
        const auto& pt = path[i];
        // Check point is not inside node bounds (without padding even)
        bool inside_node = (pt.x >= node.pos.x && pt.x <= node.pos.x + node.size.x &&
                           pt.y >= node.pos.y && pt.y <= node.pos.y + node.size.y);
        EXPECT_FALSE(inside_node) << "Intermediate point (" << pt.x << ", " << pt.y
            << ") is inside node bounds";
    }
}

// ============================================================================
// Path simplification
// ============================================================================

TEST(RouterTest, SimplifyPathStraight) {
    // Straight horizontal path should simplify to just start and end
    std::vector<GridPt> path = {
        {0,0}, {1,0}, {2,0}, {3,0}, {4,0}
    };
    auto simplified = simplify_path(path);
    EXPECT_EQ(simplified.size(), 2u);
    EXPECT_TRUE(simplified[0] == (GridPt{0,0}));
    EXPECT_TRUE(simplified[1] == (GridPt{4,0}));
}

TEST(RouterTest, SimplifyPathLShape) {
    // L-shape: horizontal then vertical
    std::vector<GridPt> path = {
        {0,0}, {1,0}, {2,0}, {2,1}, {2,2}
    };
    auto simplified = simplify_path(path);
    EXPECT_EQ(simplified.size(), 3u);
    EXPECT_TRUE(simplified[0] == (GridPt{0,0}));
    EXPECT_TRUE(simplified[1] == (GridPt{2,0}));  // corner
    EXPECT_TRUE(simplified[2] == (GridPt{2,2}));
}

TEST(RouterTest, SimplifyPathZigzag) {
    // Z-shape: right, down, right
    std::vector<GridPt> path = {
        {0,0}, {1,0}, {2,0},  // right
        {2,1}, {2,2},          // down
        {3,2}, {4,2}           // right
    };
    auto simplified = simplify_path(path);
    EXPECT_EQ(simplified.size(), 4u);  // start, corner1, corner2, end
}

TEST(RouterTest, SimplifyPathTwoPoints) {
    std::vector<GridPt> path = {{0,0}, {1,0}};
    auto simplified = simplify_path(path);
    EXPECT_EQ(simplified.size(), 2u);
}

TEST(RouterTest, SimplifyPathSinglePoint) {
    std::vector<GridPt> path = {{5,5}};
    auto simplified = simplify_path(path);
    EXPECT_EQ(simplified.size(), 1u);
}

// ============================================================================
// Wire crossing detection
// ============================================================================

TEST(RouterTest, SegmentCrossesPerpendicularWires) {
    // Horizontal and vertical segments crossing at (150, 100)
    auto pt = segment_crosses(Pt(100, 100), Pt(200, 100),
                               Pt(150, 50), Pt(150, 150));
    ASSERT_TRUE(pt.has_value());
    EXPECT_NEAR(pt->x, 150.0f, 0.1f);
    EXPECT_NEAR(pt->y, 100.0f, 0.1f);
}

TEST(RouterTest, SegmentCrossesParallel) {
    // Parallel horizontal segments - should not cross
    auto pt = segment_crosses(Pt(0, 0), Pt(100, 0),
                               Pt(0, 10), Pt(100, 10));
    EXPECT_FALSE(pt.has_value());
}

TEST(RouterTest, SegmentCrossesCollinear) {
    // Collinear segments - should not cross
    auto pt = segment_crosses(Pt(0, 0), Pt(100, 0),
                               Pt(50, 0), Pt(150, 0));
    EXPECT_FALSE(pt.has_value());
}

TEST(RouterTest, SegmentCrossesNoIntersection) {
    // Perpendicular but don't intersect
    auto pt = segment_crosses(Pt(0, 0), Pt(100, 0),
                               Pt(150, 50), Pt(150, 150));
    EXPECT_FALSE(pt.has_value());
}

TEST(RouterTest, SegmentCrossesEndpointTouch) {
    // Segments touching at endpoints should NOT be reported as crossing
    auto pt = segment_crosses(Pt(0, 0), Pt(100, 0),
                               Pt(100, 0), Pt(100, 100));
    EXPECT_FALSE(pt.has_value());
}

TEST(RouterTest, SegmentDirectionHorizontal) {
    EXPECT_EQ(segment_direction(Pt(0, 0), Pt(100, 0)), SegDir::Horiz);
}

TEST(RouterTest, SegmentDirectionVertical) {
    EXPECT_EQ(segment_direction(Pt(0, 0), Pt(0, 100)), SegDir::Vert);
}

TEST(RouterTest, FindWireCrossingsDetectsIntersection) {
    // Wire 0: horizontal, Wire 1: vertical — they cross
    std::vector<std::vector<Pt>> polylines = {
        {Pt(0, 100), Pt(200, 100)},    // wire 0: horizontal
        {Pt(100, 0), Pt(100, 200)},    // wire 1: vertical
    };

    // Wire 1 (higher index) should detect crossing with wire 0
    auto crossings = find_wire_crossings(1, polylines);
    ASSERT_EQ(crossings.size(), 1u);
    EXPECT_NEAR(crossings[0].pos.x, 100.0f, 0.5f);
    EXPECT_NEAR(crossings[0].pos.y, 100.0f, 0.5f);
    EXPECT_EQ(crossings[0].my_seg_dir, SegDir::Vert);  // wire 1 is vertical
}

TEST(RouterTest, FindWireCrossingsLowerIndexSkipped) {
    // Wire 0 should NOT detect crossing (lower index)
    std::vector<std::vector<Pt>> polylines = {
        {Pt(0, 100), Pt(200, 100)},
        {Pt(100, 0), Pt(100, 200)},
    };

    auto crossings = find_wire_crossings(0, polylines);
    EXPECT_EQ(crossings.size(), 0u);
}

TEST(RouterTest, FindWireCrossingsNoCross) {
    // Two parallel horizontal wires
    std::vector<std::vector<Pt>> polylines = {
        {Pt(0, 100), Pt(200, 100)},
        {Pt(0, 200), Pt(200, 200)},
    };

    auto crossings = find_wire_crossings(1, polylines);
    EXPECT_EQ(crossings.size(), 0u);
}

// ============================================================================
// L-shape fallback
// ============================================================================

TEST(RouterTest, LShapeBasic) {
    Pt start(0, 0);
    Pt end(100, 200);
    auto path = route_l_shape(start, end);

    ASSERT_EQ(path.size(), 3u);
    EXPECT_TRUE(path[0] == start);
    EXPECT_TRUE(path[2] == end);
    // Corner should be at (end.x, start.y) = (100, 0)
    EXPECT_NEAR(path[1].x, 100.0f, 0.1f);
    EXPECT_NEAR(path[1].y, 0.0f, 0.1f);
}

TEST(RouterTest, LShapeSameY) {
    // When start and end have same Y, corner = end → should collapse to 2 points
    Pt start(0, 100);
    Pt end(200, 100);
    auto path = route_l_shape(start, end);

    EXPECT_LE(path.size(), 3u);
    EXPECT_TRUE(path.front() == start);
    EXPECT_TRUE(path.back() == end);
}

TEST(RouterTest, LShapeSamePoint) {
    Pt p(50, 50);
    auto path = route_l_shape(p, p);
    // start == end, corner == both → degenerate
    EXPECT_GE(path.size(), 2u);
    EXPECT_TRUE(path.front() == p);
    EXPECT_TRUE(path.back() == p);
}

// ============================================================================
// Port departure direction
// ============================================================================

TEST(RouterTest, PortDepartureInput) {
    Node n;
    n.id = "n1";
    n.pos = Pt(100, 100);
    n.size = Pt(120, 80);
    n.input("v_in");
    n.output("v_out");

    auto dir = get_port_departure(n, "v_in");
    EXPECT_EQ(dir.dx, -1);  // input → depart left
    EXPECT_EQ(dir.dy, 0);
}

TEST(RouterTest, PortDepartureOutput) {
    Node n;
    n.id = "n1";
    n.pos = Pt(100, 100);
    n.size = Pt(120, 80);
    n.input("v_in");
    n.output("v_out");

    auto dir = get_port_departure(n, "v_out");
    EXPECT_EQ(dir.dx, +1);  // output → depart right
    EXPECT_EQ(dir.dy, 0);
}

TEST(RouterTest, PortDepartureRef) {
    Node n;
    n.id = "ref";
    n.render_hint = "ref";
    n.pos = Pt(100, 100);
    n.size = Pt(120, 80);
    n.output("v");

    auto dir = get_port_departure(n, "v");
    EXPECT_EQ(dir.dx, 0);
    EXPECT_EQ(dir.dy, -1);  // ref → depart upward
}

TEST(RouterTest, PortDepartureBus) {
    Node n;
    n.id = "bus";
    n.render_hint = "bus";
    n.pos = Pt(100, 100);
    n.size = Pt(120, 80);
    n.output("v");

    auto dir = get_port_departure(n, "v");
    EXPECT_EQ(dir.dx, 0);  // bus → no preferred direction
    EXPECT_EQ(dir.dy, 0);
}

TEST(RouterTest, RouteWithPortDeparture) {
    // Route from an output port (right side) to an input port (left side)
    // Path should start by going RIGHT from start, then LEFT into end
    Node n1;
    n1.id = "n1";
    n1.pos = Pt(0, 0);
    n1.size = Pt(120, 80);
    n1.output("out");

    Node n2;
    n2.id = "n2";
    n2.pos = Pt(300, 0);
    n2.size = Pt(120, 80);
    n2.input("in");

    std::vector<Node> nodes = {n1, n2};
    std::vector<Wire> wires;

    VisualNodeCache cache;
    Pt start_pos = editor_math::get_port_position(n1, "out", wires, nullptr, cache);  // right side of n1
    Pt end_pos = editor_math::get_port_position(n2, "in", wires, nullptr, cache);      // left side of n2

    auto path = route_around_nodes(start_pos, end_pos, n1, "out", n2, "in", nodes, 16.0f);
    ASSERT_FALSE(path.empty());
    ASSERT_GE(path.size(), 2u);

    // First point is the port position
    EXPECT_TRUE(path.front() == start_pos);
    EXPECT_TRUE(path.back() == end_pos);

    // Second point should be to the RIGHT of start (departure pad)
    if (path.size() >= 3) {
        EXPECT_GT(path[1].x, start_pos.x) << "Wire should depart to the right from output port";
    }
}
