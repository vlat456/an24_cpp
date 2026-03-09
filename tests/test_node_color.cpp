#include <gtest/gtest.h>
#include "editor/data/node.h"
#include "editor/visual/scene/persist.h"
#include "editor/visual/node/node.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/viewport/viewport.h"
#include "editor/visual/renderer/mock_draw_list.h"

// =============================================================================
// Phase 1.1: Data model tests
// =============================================================================

// Node color should default to "no custom color" (nullopt)
TEST(NodeColor, DefaultColor_IsNullopt) {
    Node n;
    EXPECT_FALSE(n.color.has_value());
}

// Node color can be set
TEST(NodeColor, SetColor) {
    Node n;
    n.color = NodeColor{0.5f, 0.3f, 0.8f, 1.0f};
    ASSERT_TRUE(n.color.has_value());
    EXPECT_FLOAT_EQ(n.color->r, 0.5f);
    EXPECT_FLOAT_EQ(n.color->g, 0.3f);
    EXPECT_FLOAT_EQ(n.color->b, 0.8f);
    EXPECT_FLOAT_EQ(n.color->a, 1.0f);
}

// to_uint32 converts to ImGui ABGR format (0xAABBGGRR)
TEST(NodeColor, ToUint32_ABGR) {
    NodeColor c{1.0f, 0.0f, 0.0f, 1.0f}; // pure red
    EXPECT_EQ(c.to_uint32(), 0xFF0000FFu); // ABGR: alpha=FF, blue=00, green=00, red=FF
}

TEST(NodeColor, ToUint32_Green) {
    NodeColor c{0.0f, 1.0f, 0.0f, 1.0f};
    EXPECT_EQ(c.to_uint32(), 0xFF00FF00u);
}

TEST(NodeColor, ToUint32_HalfAlpha) {
    NodeColor c{1.0f, 1.0f, 1.0f, 0.5f};
    uint32_t result = c.to_uint32();
    uint8_t alpha = (result >> 24) & 0xFF;
    EXPECT_NEAR(alpha, 127, 1); // 0.5 * 255 ≈ 127
}

// =============================================================================
// Phase 1.2: JSON persistence roundtrip tests
// =============================================================================

// Node with no color → JSON has no "color" key
TEST(NodeColorPersist, NoColor_NotInJson) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(0, 0);
    n.input("v_in"); n.output("v_out");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    EXPECT_EQ(json.find("\"color\""), std::string::npos)
        << "Node without custom color should not emit color key";
}

// Node with color → roundtrips through JSON
TEST(NodeColorPersist, Color_Roundtrip) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(100, 200);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.5f, 0.3f, 0.8f, 1.0f};
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1u);
    ASSERT_TRUE(bp2->nodes[0].color.has_value());
    EXPECT_NEAR(bp2->nodes[0].color->r, 0.5f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->g, 0.3f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->b, 0.8f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->a, 1.0f, 0.01f);
}

// Node color loads from JSON that has "color" field
TEST(NodeColorPersist, LoadFromJson_WithColor) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}, "v_out": {"direction": "Out", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80},
            "color": {"r": 0.2, "g": 0.4, "b": 0.6, "a": 1.0}
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_TRUE(bp->nodes[0].color.has_value());
    EXPECT_NEAR(bp->nodes[0].color->r, 0.2f, 0.001f);
}

// JSON without color field → no custom color
TEST(NodeColorPersist, LoadFromJson_WithoutColor) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}, "v_out": {"direction": "Out", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80}
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_FALSE(bp->nodes[0].color.has_value());
}

// Multiple nodes: only colored one gets color
TEST(NodeColorPersist, MultipleNodes_OnlyColoredOneHasColor) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery"; n1.at(0,0);
    n1.input("v_in"); n1.output("v_out");
    n1.color = NodeColor{1.0f, 0.0f, 0.0f, 1.0f};

    Node n2; n2.id = "b"; n2.type_name = "Resistor"; n2.at(200,0);
    n2.input("v_in");
    // n2 has NO color

    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_TRUE(bp2->nodes[0].color.has_value());
    EXPECT_FALSE(bp2->nodes[1].color.has_value());
}

// =============================================================================
// Phase 1.3: VisualNode color application tests
// =============================================================================

// VisualNode should store custom color if Node has one
TEST(NodeColorVisual, CustomColor_StoredInVisualNode) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.2f, 0.4f, 0.6f, 1.0f};

    VisualNode vn(n);
    // VisualNode should expose the custom color
    ASSERT_TRUE(vn.customColor().has_value());
    EXPECT_FLOAT_EQ(vn.customColor()->r, 0.2f);
}

// VisualNode without custom color should return nullopt
TEST(NodeColorVisual, NoCustomColor_Nullopt) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");

    VisualNode vn(n);
    EXPECT_FALSE(vn.customColor().has_value());
}

// =============================================================================
// Phase 2: Edge cases and regression tests
// =============================================================================

// --- to_uint32 additional colors ---

TEST(NodeColor, ToUint32_Blue) {
    NodeColor c{0.0f, 0.0f, 1.0f, 1.0f};
    EXPECT_EQ(c.to_uint32(), 0xFFFF0000u); // ABGR: A=FF, B=FF, G=00, R=00
}

TEST(NodeColor, ToUint32_White) {
    NodeColor c{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(c.to_uint32(), 0xFFFFFFFFu);
}

TEST(NodeColor, ToUint32_Black) {
    NodeColor c{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_EQ(c.to_uint32(), 0xFF000000u);
}

TEST(NodeColor, ToUint32_ZeroAlpha) {
    NodeColor c{1.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_EQ(c.to_uint32(), 0x000000FFu); // fully transparent red
}

// --- Regression: negative/overflow values must clamp, not UB ---

TEST(NodeColor, ToUint32_NegativeClamps) {
    NodeColor c{-0.5f, -1.0f, 0.5f, 1.0f};
    uint32_t result = c.to_uint32();
    uint8_t r = result & 0xFF;
    uint8_t g = (result >> 8) & 0xFF;
    EXPECT_EQ(r, 0);   // clamped from -0.5
    EXPECT_EQ(g, 0);   // clamped from -1.0
}

TEST(NodeColor, ToUint32_OverflowClamps) {
    NodeColor c{2.0f, 1.5f, 0.5f, 1.0f};
    uint32_t result = c.to_uint32();
    uint8_t r = result & 0xFF;
    uint8_t g = (result >> 8) & 0xFF;
    EXPECT_EQ(r, 255); // clamped from 2.0
    EXPECT_EQ(g, 255); // clamped from 1.5
}

// --- to_uint32 default NodeColor ---

TEST(NodeColor, ToUint32_Default) {
    NodeColor c; // defaults: 0.5, 0.5, 0.5, 1.0
    uint32_t result = c.to_uint32();
    uint8_t r = result & 0xFF;
    uint8_t g = (result >> 8) & 0xFF;
    uint8_t b = (result >> 16) & 0xFF;
    uint8_t a = (result >> 24) & 0xFF;
    EXPECT_NEAR(r, 128, 1);
    EXPECT_NEAR(g, 128, 1);
    EXPECT_NEAR(b, 128, 1);
    EXPECT_EQ(a, 255);
}

// --- setCustomColor direct test ---

TEST(NodeColorVisual, SetCustomColor_SetAndClear) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");

    VisualNode vn(n);
    EXPECT_FALSE(vn.customColor().has_value());

    // Set a color
    vn.setCustomColor(NodeColor{1.0f, 0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(vn.customColor().has_value());
    EXPECT_FLOAT_EQ(vn.customColor()->r, 1.0f);

    // Clear back to nullopt
    vn.setCustomColor(std::nullopt);
    EXPECT_FALSE(vn.customColor().has_value());
}

// --- JSON persistence edge cases ---

// Color with zero alpha roundtrips correctly
TEST(NodeColorPersist, ZeroAlpha_Roundtrip) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(0, 0);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{1.0f, 0.0f, 0.0f, 0.0f};
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_TRUE(bp2->nodes[0].color.has_value());
    EXPECT_NEAR(bp2->nodes[0].color->a, 0.0f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->r, 1.0f, 0.01f);
}

// JSON with color as non-object (string) → should not crash, no color set
TEST(NodeColorPersist, MalformedColor_String_Ignored) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80},
            "color": "red"
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_FALSE(bp->nodes[0].color.has_value());
}

// JSON with color as null → should not crash, no color set
TEST(NodeColorPersist, MalformedColor_Null_Ignored) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80},
            "color": null
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_FALSE(bp->nodes[0].color.has_value());
}

// JSON with color object missing some keys → defaults used
TEST(NodeColorPersist, PartialColor_DefaultsApplied) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80},
            "color": {"r": 0.9}
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_TRUE(bp->nodes[0].color.has_value());
    EXPECT_NEAR(bp->nodes[0].color->r, 0.9f, 0.01f);
    EXPECT_NEAR(bp->nodes[0].color->g, 0.5f, 0.01f); // default
    EXPECT_NEAR(bp->nodes[0].color->b, 0.5f, 0.01f); // default
    EXPECT_NEAR(bp->nodes[0].color->a, 1.0f, 0.01f); // default
}

// JSON output contains expected key names
TEST(NodeColorPersist, JsonKeyNames) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(0, 0);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.1f, 0.2f, 0.3f, 0.4f};
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    EXPECT_NE(json.find("\"color\""), std::string::npos);
    EXPECT_NE(json.find("\"r\""), std::string::npos);
    EXPECT_NE(json.find("\"g\""), std::string::npos);
    EXPECT_NE(json.find("\"b\""), std::string::npos);
    EXPECT_NE(json.find("\"a\""), std::string::npos);
}

// Double roundtrip stability
TEST(NodeColorPersist, DoubleRoundtrip_Stable) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(100, 200);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.123f, 0.456f, 0.789f, 0.999f};
    bp.add_node(std::move(n));

    std::string json1 = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json1);
    ASSERT_TRUE(bp2.has_value());
    std::string json2 = blueprint_to_editor_json(*bp2);
    auto bp3 = blueprint_from_json(json2);
    ASSERT_TRUE(bp3.has_value());

    ASSERT_TRUE(bp3->nodes[0].color.has_value());
    EXPECT_NEAR(bp3->nodes[0].color->r, 0.123f, 0.01f);
    EXPECT_NEAR(bp3->nodes[0].color->g, 0.456f, 0.01f);
    EXPECT_NEAR(bp3->nodes[0].color->b, 0.789f, 0.01f);
    EXPECT_NEAR(bp3->nodes[0].color->a, 0.999f, 0.01f);
}

// =============================================================================
// Phase 3: Regression tests — Bus/Ref nodes use custom color
// =============================================================================

// Helper: create a bus node with custom color
static Node make_bus_node(std::optional<NodeColor> color = std::nullopt) {
    Node n;
    n.id = "bus1"; n.name = "Bus1"; n.type_name = "Bus";
    n.render_hint = "bus";
    n.at(0, 0).size_wh(200, 40);
    n.input("v");
    if (color) n.color = *color;
    return n;
}

// Helper: create a ref node with custom color
static Node make_ref_node(std::optional<NodeColor> color = std::nullopt) {
    Node n;
    n.id = "ref1"; n.name = "Ref1"; n.type_name = "RefNode";
    n.render_hint = "ref";
    n.at(0, 0).size_wh(60, 30);
    n.output("v");
    if (color) n.color = *color;
    return n;
}

// Regression: BusVisualNode constructor stores custom color from Node
TEST(NodeColorBusRegression, BusNode_StoresCustomColor) {
    Node n = make_bus_node(NodeColor{1.0f, 0.0f, 0.0f, 1.0f});
    BusVisualNode bus(n);
    ASSERT_TRUE(bus.customColor().has_value());
    EXPECT_FLOAT_EQ(bus.customColor()->r, 1.0f);
    EXPECT_FLOAT_EQ(bus.customColor()->g, 0.0f);
}

// Regression: BusVisualNode without color → nullopt
TEST(NodeColorBusRegression, BusNode_NoColor_Nullopt) {
    Node n = make_bus_node();
    BusVisualNode bus(n);
    EXPECT_FALSE(bus.customColor().has_value());
}

// Regression: BusVisualNode renders with custom color (not hardcoded COLOR_BUS_FILL)
TEST(NodeColorBusRegression, BusNode_RendersCustomColor) {
    NodeColor red{1.0f, 0.0f, 0.0f, 1.0f};
    Node n = make_bus_node(red);
    BusVisualNode bus(n);

    MockDrawList dl;
    Viewport vp;
    bus.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(red.to_uint32()))
        << "BusVisualNode should render with custom color, not COLOR_BUS_FILL";
}

// Regression: BusVisualNode without color renders with default COLOR_BUS_FILL
TEST(NodeColorBusRegression, BusNode_NoColor_RendersDefault) {
    Node n = make_bus_node();
    BusVisualNode bus(n);

    MockDrawList dl;
    Viewport vp;
    bus.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(render_theme::COLOR_BUS_FILL))
        << "BusVisualNode without custom color should use COLOR_BUS_FILL";
}

// Regression: RefVisualNode constructor stores custom color
TEST(NodeColorRefRegression, RefNode_StoresCustomColor) {
    Node n = make_ref_node(NodeColor{0.0f, 1.0f, 0.0f, 1.0f});
    RefVisualNode ref(n);
    ASSERT_TRUE(ref.customColor().has_value());
    EXPECT_FLOAT_EQ(ref.customColor()->g, 1.0f);
}

// Regression: RefVisualNode renders with custom color
TEST(NodeColorRefRegression, RefNode_RendersCustomColor) {
    NodeColor green{0.0f, 1.0f, 0.0f, 1.0f};
    Node n = make_ref_node(green);
    RefVisualNode ref(n);

    MockDrawList dl;
    Viewport vp;
    ref.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(green.to_uint32()))
        << "RefVisualNode should render with custom color, not COLOR_BUS_FILL";
}

// Regression: RefVisualNode without color renders default
TEST(NodeColorRefRegression, RefNode_NoColor_RendersDefault) {
    Node n = make_ref_node();
    RefVisualNode ref(n);

    MockDrawList dl;
    Viewport vp;
    ref.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(render_theme::COLOR_BUS_FILL))
        << "RefVisualNode without custom color should use COLOR_BUS_FILL";
}

// Regression: VisualNode body renders with custom color
TEST(NodeColorVisual, BodyFill_UsesCustomColor) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");
    NodeColor blue{0.0f, 0.0f, 1.0f, 1.0f};
    n.color = blue;

    VisualNode vn(n);
    MockDrawList dl;
    Viewport vp;
    vn.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(blue.to_uint32()))
        << "VisualNode body should use custom color";
}

// Regression: VisualNode without color uses default body fill
TEST(NodeColorVisual, BodyFill_UsesDefault) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");

    VisualNode vn(n);
    MockDrawList dl;
    Viewport vp;
    vn.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(render_theme::COLOR_BODY_FILL))
        << "VisualNode without custom color should use COLOR_BODY_FILL";
}

// Regression: setCustomColor on BusVisualNode updates rendering
TEST(NodeColorBusRegression, BusNode_SetCustomColor_UpdatesRender) {
    Node n = make_bus_node();
    BusVisualNode bus(n);

    NodeColor red{1.0f, 0.0f, 0.0f, 1.0f};
    bus.setCustomColor(red);

    MockDrawList dl;
    Viewport vp;
    bus.render(&dl, vp, Pt(0, 0), false);

    EXPECT_TRUE(dl.has_rect_filled_with_color(red.to_uint32()))
        << "After setCustomColor, BusVisualNode should render with new color";
}

// Regression: VisualNodeFactory preserves custom color for bus nodes
TEST(NodeColorBusRegression, Factory_BusNode_PreservesColor) {
    NodeColor cyan{0.0f, 1.0f, 1.0f, 1.0f};
    Node n = make_bus_node(cyan);
    auto vn = VisualNodeFactory::create(n);
    ASSERT_TRUE(vn->customColor().has_value());
    EXPECT_FLOAT_EQ(vn->customColor()->r, 0.0f);
    EXPECT_FLOAT_EQ(vn->customColor()->g, 1.0f);
    EXPECT_FLOAT_EQ(vn->customColor()->b, 1.0f);
}

// Regression: VisualNodeFactory preserves custom color for ref nodes
TEST(NodeColorRefRegression, Factory_RefNode_PreservesColor) {
    NodeColor magenta{1.0f, 0.0f, 1.0f, 1.0f};
    Node n = make_ref_node(magenta);
    auto vn = VisualNodeFactory::create(n);
    ASSERT_TRUE(vn->customColor().has_value());
    EXPECT_FLOAT_EQ(vn->customColor()->r, 1.0f);
    EXPECT_FLOAT_EQ(vn->customColor()->b, 1.0f);
}
