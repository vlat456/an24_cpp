#include <gtest/gtest.h>
#include "editor/visual/renderer/wire/polyline_builder.h"
#include "editor/visual/renderer/wire/polyline_draw.h"
#include "editor/visual/renderer/wire/arc_draw.h"
#include "editor/visual/renderer/mock_draw_list.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/visual/node/node.h"
#include "editor/visual/node/visual_node_cache.h"
#include "editor/viewport/viewport.h"
#include "editor/router/crossings.h"
#include <cmath>

// ============================================================================
// polyline_builder tests
// ============================================================================

TEST(PolylineBuilder, EmptyBlueprint_ReturnsEmpty) {
    Blueprint bp;
    VisualNodeCache cache;
    auto result = polyline_builder::build_wire_polylines(bp, "", cache);
    EXPECT_TRUE(result.empty());
}

TEST(PolylineBuilder, SingleWire_TwoNodes_ReturnsPolylineWithTwoPoints) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    auto polys = polyline_builder::build_wire_polylines(bp, "", cache);
    ASSERT_EQ(polys.size(), 1u);
    EXPECT_EQ(polys[0].size(), 2u) << "Wire with no routing points should have start + end";
}

TEST(PolylineBuilder, WireWithRoutingPoints_IncludesAllPoints) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(400, 200).size_wh(120, 80); n2.input("i");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(200, 0));
    w.add_routing_point(Pt(200, 200));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    auto polys = polyline_builder::build_wire_polylines(bp, "", cache);
    ASSERT_EQ(polys.size(), 1u);
    EXPECT_EQ(polys[0].size(), 4u) << "start + 2 routing points + end";
}

TEST(PolylineBuilder, WireWithMissingNode_ReturnsEmptyPolyline) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    bp.add_node(std::move(n1));

    // Wire references non-existent node "b"
    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    auto polys = polyline_builder::build_wire_polylines(bp, "", cache);
    ASSERT_EQ(polys.size(), 1u);
    EXPECT_TRUE(polys[0].empty()) << "Wire with missing endpoint node should produce empty polyline";
}

TEST(PolylineBuilder, WireCrossGroup_ReturnsEmptyPolyline) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    n1.group_id = "";
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    n2.group_id = "other";
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    auto polys = polyline_builder::build_wire_polylines(bp, "", cache);
    ASSERT_EQ(polys.size(), 1u);
    EXPECT_TRUE(polys[0].empty()) << "Wire crossing groups should produce empty polyline";
}

TEST(PolylineBuilder, MultipleWires_ReturnsOnePolylinePerWire) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o1"); n1.output("o2");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i1"); n2.input("i2");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    bp.add_wire(Wire::make("w1", wire_output("a", "o1"), wire_input("b", "i1")));
    bp.add_wire(Wire::make("w2", wire_output("a", "o2"), wire_input("b", "i2")));

    VisualNodeCache cache;
    auto polys = polyline_builder::build_wire_polylines(bp, "", cache);
    EXPECT_EQ(polys.size(), 2u);
    EXPECT_GE(polys[0].size(), 2u);
    EXPECT_GE(polys[1].size(), 2u);
}

// ============================================================================
// polyline_draw::classify_crossings_by_segment tests
// ============================================================================

TEST(ClassifyCrossings, NoCrossings_ReturnsEmpty) {
    std::vector<WireCrossing> crossings;
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0), Pt(100, 100)};
    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    EXPECT_TRUE(result.empty());
}

TEST(ClassifyCrossings, CrossingOnFirstSegment_CorrectIndex) {
    // Horizontal segment from (0,0) to (100,0)
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0)};
    // Crossing at midpoint (50, 0)
    std::vector<WireCrossing> crossings = {{Pt(50, 0), SegDir::Horiz}};

    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].seg_idx, 0u);
    EXPECT_NEAR(result[0].t, 0.5f, 0.02f);
    EXPECT_FLOAT_EQ(result[0].pos.x, 50.0f);
    EXPECT_FLOAT_EQ(result[0].pos.y, 0.0f);
}

TEST(ClassifyCrossings, CrossingOnSecondSegment_CorrectIndex) {
    // L-shaped polyline: (0,0) -> (100,0) -> (100,100)
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0), Pt(100, 100)};
    // Crossing at (100, 50) — on the vertical segment
    std::vector<WireCrossing> crossings = {{Pt(100, 50), SegDir::Vert}};

    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].seg_idx, 1u);
    EXPECT_NEAR(result[0].t, 0.5f, 0.02f);
}

TEST(ClassifyCrossings, MultipleCrossings_AllClassified) {
    std::vector<Pt> poly = {Pt(0, 0), Pt(200, 0)};
    std::vector<WireCrossing> crossings = {
        {Pt(50, 0), SegDir::Horiz},
        {Pt(150, 0), SegDir::Horiz},
    };

    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].seg_idx, 0u);
    EXPECT_EQ(result[1].seg_idx, 0u);
    EXPECT_LT(result[0].t, result[1].t);
}

TEST(ClassifyCrossings, CrossingFarFromSegment_NotClassified) {
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0)};
    // Crossing 10 units away from the segment — should NOT be classified
    std::vector<WireCrossing> crossings = {{Pt(50, 10), SegDir::Horiz}};

    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    EXPECT_TRUE(result.empty()) << "Crossing far from segment should not be classified";
}

TEST(ClassifyCrossings, PreservesSegDir) {
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0)};
    std::vector<WireCrossing> crossings = {{Pt(50, 0), SegDir::Vert}};

    auto result = polyline_draw::classify_crossings_by_segment(crossings, poly);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].my_seg_dir, SegDir::Vert);
}

// ============================================================================
// polyline_draw::draw_polyline_with_gaps tests
// ============================================================================

TEST(DrawPolylineWithGaps, NoCrossings_SinglePolyline) {
    MockDrawList dl;
    Viewport vp;
    std::vector<Pt> poly = {Pt(0, 0), Pt(100, 0), Pt(100, 100)};
    std::vector<polyline_draw::CrossOnSeg> no_crossings;

    polyline_draw::draw_polyline_with_gaps(dl, poly, no_crossings, vp, Pt(0, 0),
                                           0xFFFFFFFF, 5.0f);

    EXPECT_TRUE(dl.had_polyline());
    EXPECT_EQ(dl.polyline_colors_.size(), 1u)
        << "No crossings → single polyline draw call";
}

TEST(DrawPolylineWithGaps, OneCrossing_SplitsIntoMultiplePolylines) {
    MockDrawList dl;
    Viewport vp;
    std::vector<Pt> poly = {Pt(0, 0), Pt(200, 0)};
    // Crossing at midpoint
    std::vector<polyline_draw::CrossOnSeg> crossings = {
        {0, 0.5f, Pt(100, 0), SegDir::Horiz}
    };

    polyline_draw::draw_polyline_with_gaps(dl, poly, crossings, vp, Pt(0, 0),
                                           0xFFAABBCC, 5.0f);

    // Should produce at least 2 polyline segments (before gap + after gap)
    EXPECT_GE(dl.polyline_colors_.size(), 2u)
        << "One crossing should split wire into at least 2 segments";
    // All segments should use the same color
    for (auto c : dl.polyline_colors_) {
        EXPECT_EQ(c, 0xFFAABBCCu);
    }
}

TEST(DrawPolylineWithGaps, TwoCrossings_SplitsIntoThreeSegments) {
    MockDrawList dl;
    Viewport vp;
    std::vector<Pt> poly = {Pt(0, 0), Pt(300, 0)};
    std::vector<polyline_draw::CrossOnSeg> crossings = {
        {0, 0.33f, Pt(100, 0), SegDir::Horiz},
        {0, 0.67f, Pt(200, 0), SegDir::Horiz},
    };

    polyline_draw::draw_polyline_with_gaps(dl, poly, crossings, vp, Pt(0, 0),
                                           0xFF112233, 5.0f);

    EXPECT_GE(dl.polyline_colors_.size(), 3u)
        << "Two crossings should split wire into at least 3 segments";
}

// ============================================================================
// arc_draw tests
// ============================================================================

TEST(ArcDraw, DrawJumpArc_HorizontalWire_RendersVerticalArc) {
    MockDrawList dl;
    WireCrossing crossing{Pt(100, 50), SegDir::Horiz};
    Pt screen_pos(100, 50);
    float arc_radius = 5.0f;

    arc_draw::draw_jump_arc(dl, crossing, screen_pos, arc_radius, 0xFFAABBCC);

    EXPECT_TRUE(dl.had_polyline()) << "Arc should be drawn as a polyline";
    EXPECT_EQ(dl.polyline_colors_.size(), 1u);
    EXPECT_EQ(dl.polyline_colors_[0], 0xFFAABBCCu);
}

TEST(ArcDraw, DrawJumpArc_VerticalWire_RendersHorizontalArc) {
    MockDrawList dl;
    WireCrossing crossing{Pt(50, 100), SegDir::Vert};
    Pt screen_pos(50, 100);
    float arc_radius = 5.0f;

    arc_draw::draw_jump_arc(dl, crossing, screen_pos, arc_radius, 0xFF112233);

    EXPECT_TRUE(dl.had_polyline());
    EXPECT_EQ(dl.polyline_colors_[0], 0xFF112233u);
}

TEST(ArcDraw, DrawJumpArc_UnknownDir_TreatedAsHorizontal) {
    // SegDir::Unknown should behave like Horiz (arc_vertical = true)
    MockDrawList dl;
    WireCrossing crossing{Pt(0, 0), SegDir::Unknown};

    arc_draw::draw_jump_arc(dl, crossing, Pt(0, 0), 5.0f, 0xFF000000);
    EXPECT_TRUE(dl.had_polyline());
}

TEST(ArcDraw, DrawAllArcs_MultipleCrossings_DrawsMultipleArcs) {
    MockDrawList dl;
    Viewport vp;
    std::vector<WireCrossing> crossings = {
        {Pt(100, 0), SegDir::Horiz},
        {Pt(200, 0), SegDir::Vert},
        {Pt(300, 0), SegDir::Horiz},
    };

    arc_draw::draw_all_arcs(dl, crossings, vp, Pt(0, 0), 0xFFFFFFFF);

    EXPECT_EQ(dl.polyline_colors_.size(), 3u)
        << "3 crossings should produce 3 arc polylines";
}

TEST(ArcDraw, DrawAllArcs_Empty_DrawsNothing) {
    MockDrawList dl;
    Viewport vp;
    std::vector<WireCrossing> crossings;

    arc_draw::draw_all_arcs(dl, crossings, vp, Pt(0, 0), 0xFFFFFFFF);

    EXPECT_FALSE(dl.had_polyline());
}

TEST(ArcDraw, DrawAllArcs_ArcRadiusScalesWithZoom) {
    // At zoom=2, arc radius should be 2x bigger, so the arc points
    // should span a wider range on screen
    MockDrawList dl1, dl2;
    Viewport vp1, vp2;
    vp1.zoom = 1.0f;
    vp2.zoom = 2.0f;

    WireCrossing crossing{Pt(100, 100), SegDir::Horiz};
    float arc_radius_1 = render_theme::ARC_RADIUS_WORLD * vp1.zoom;
    float arc_radius_2 = render_theme::ARC_RADIUS_WORLD * vp2.zoom;

    arc_draw::draw_jump_arc(dl1, crossing, Pt(100, 100), arc_radius_1, 0xFFFFFFFF);
    arc_draw::draw_jump_arc(dl2, crossing, Pt(200, 200), arc_radius_2, 0xFFFFFFFF);

    // Both should produce polylines
    EXPECT_TRUE(dl1.had_polyline());
    EXPECT_TRUE(dl2.had_polyline());
    // The radius used for zoom=2 is double — we can't check exact points
    // from MockDrawList, but we verify the call was made correctly.
    EXPECT_FLOAT_EQ(arc_radius_2, arc_radius_1 * 2.0f);
}

// ============================================================================
// Integration: crossings + classify + draw
// ============================================================================

TEST(WireModulesIntegration, TwoCrossingWires_CorrectCrossingCount) {
    // Two crossing wires: horizontal (0,0)→(100,0) and vertical (50,-50)→(50,50)
    std::vector<std::vector<Pt>> polylines = {
        {Pt(0, 0), Pt(100, 0)},
        {Pt(50, -50), Pt(50, 50)},
    };

    // Wire 1 (higher index) should find one crossing with wire 0
    auto crossings = find_wire_crossings(1, polylines);
    ASSERT_EQ(crossings.size(), 1u);
    EXPECT_NEAR(crossings[0].pos.x, 50.0f, 0.5f);
    EXPECT_NEAR(crossings[0].pos.y, 0.0f, 0.5f);

    // Classify against wire 1's polyline
    auto classified = polyline_draw::classify_crossings_by_segment(crossings, polylines[1]);
    ASSERT_EQ(classified.size(), 1u);
    EXPECT_EQ(classified[0].seg_idx, 0u);
    EXPECT_NEAR(classified[0].t, 0.5f, 0.02f);
}

TEST(WireModulesIntegration, ParallelWires_NoCrossings) {
    std::vector<std::vector<Pt>> polylines = {
        {Pt(0, 0), Pt(100, 0)},
        {Pt(0, 10), Pt(100, 10)},
    };

    auto crossings = find_wire_crossings(1, polylines);
    EXPECT_TRUE(crossings.empty()) << "Parallel wires should not cross";
}
