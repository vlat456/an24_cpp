#include <gtest/gtest.h>
#include "editor/visual/node/node.h"
#include "editor/visual/node/visual_node_cache.h"
#include "editor/visual/node/widget.h"
#include "editor/visual/renderer/mock_draw_list.h"
#include "editor/data/node.h"


// ============================================================================
// Auto-size Tests: Node should be minimum size to fit content + ports + padding
// ============================================================================

TEST(NodeAutoSizeTest, DefaultSize_NoPorts_NoContent) {
    // Node without explicit size should auto-size to minimum
    Node n;
    n.id = "test";
    n.name = "Simple";
    n.type_name = "SimpleComponent";

    VisualNode visual(n);

    Pt size = visual.getSize();
    // Should be at least: header(24) + typename(16) = 40px
    // Snapped to 16px grid: 48px
    EXPECT_GE(size.y, 40.0f) << "Node height should fit header + typename";
    EXPECT_GE(size.x, HeaderWidget::VISUAL_HEIGHT * 2) << "Node width should fit header text";
}

TEST(NodeAutoSizeTest, LongPortNames_ExpandWidth) {
    // Node with long port names should auto-size width
    Node n;
    n.id = "test";
    n.name = "Regulator";
    n.type_name = "PowerRegulator";
    n.input("main_power_input_24v");
    n.output("regulated_output_with_filter");

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Calculate expected width:
    // Left: PORT_RADIUS(6) + GAP(3) + label_width(~120) = ~129
    // Content: at least 20px
    // Right: label_width(~200) + GAP(3) + PORT_RADIUS(6) = ~209
    // Total: ~129 + 20 + 209 = ~358px
    // Snapped to 16px grid: ~368px

    EXPECT_GT(size.x, 300.0f) << "Node width should expand for long port names";
    EXPECT_LT(size.x, 500.0f) << "Node width should be reasonable";
}

TEST(NodeAutoSizeTest, GaugeContent_SetsMinimumWidth) {
    // Node with gauge should have minimum width for gauge
    Node n;
    n.id = "voltmeter";
    n.name = "VMeter";
    n.type_name = "Voltmeter";
    n.input("v_in");
    n.output("v_out");
    n.node_content.type = NodeContentType::Gauge;
    n.node_content.min = 0.0f;
    n.node_content.max = 30.0f;
    n.node_content.unit = "V";

    VisualNode visual(n);

    Pt size = visual.getSize();

    // VoltmeterWidget requires GAUGE_RADIUS * 2 = 80px minimum
    EXPECT_GE(size.x, VoltmeterWidget::GAUGE_RADIUS * 2.0f)
        << "Node width should fit gauge (80px minimum)";
}

TEST(NodeAutoSizeTest, ManyPorts_ExpandHeight) {
    // Node with many ports should auto-size height
    Node n;
    n.id = "multi";
    n.name = "MultiPort";
    n.type_name = "MultiPort";
    // 8 input ports
    n.input("in1");
    n.input("in2");
    n.input("in3");
    n.input("in4");
    n.input("in5");
    n.input("in6");
    n.input("in7");
    n.input("in8");
    // 2 output ports
    n.output("out1");
    n.output("out2");

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Expected height:
    // header(24) + 8 port rows(8*16=128) + typename(16) = 168px
    // Snapped to 16px grid: 176px
    EXPECT_GE(size.y, 160.0f) << "Node height should fit all port rows";
}

TEST(NodeAutoSizeTest, ExplicitSize_LargerThanMinimum) {
    // If explicit size is larger than minimum, use explicit size
    Node n;
    n.id = "test";
    n.name = "Small";
    n.type_name = "SmallComponent";
    n.input("in");
    n.output("out");
    n.at(0, 0).size_wh(200, 150);  // Explicitly larger than needed

    VisualNode visual(n);

    Pt size = visual.getSize();

    EXPECT_FLOAT_EQ(size.x, 200.0f) << "Should use explicit width when larger";
    EXPECT_FLOAT_EQ(size.y, 150.0f) << "Should use explicit height when larger";
}

TEST(NodeAutoSizeTest, ExplicitSize_TooSmall_LogsWarning) {
    // If explicit size is smaller than minimum, warning should be logged
    // This test expects the warning but doesn't fail if size is clamped
    Node n;
    n.id = "test";
    n.name = "ComponentWithVeryLongNameThatNeedsSpace";
    n.type_name = "LongTypeNameComponent";
    n.input("very_long_input_port_name");
    n.output("very_long_output_port_name");
    n.at(0, 0).size_wh(80, 40);  // Explicitly too small

    // Capture log output (would need spdlog setup)
    // For now, just verify node is created and doesn't crash
    VisualNode visual(n);

    Pt size = visual.getSize();

    // Node should either:
    // 1. Use the explicit (too small) size - user's responsibility
    // 2. OR clamp to minimum - implementation choice
    // Either way, no crash
    EXPECT_GT(size.x, 0.0f) << "Node should have positive width";
    EXPECT_GT(size.y, 0.0f) << "Node should have positive height";
}

TEST(NodeAutoSizeTest, SwitchContent_MinimumWidth) {
    // Switch content should have reasonable minimum width
    Node n;
    n.id = "switch1";
    n.name = "Switch";
    n.type_name = "Switch";
    n.input("ctrl");
    n.output("state");
    n.node_content.type = NodeContentType::Switch;
    n.node_content.label = "ON/OFF";

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Switch should be at least 60px wide
    EXPECT_GE(size.x, 60.0f) << "Switch should have minimum width for UI element";
}

TEST(NodeAutoSizeTest, ValueContent_ExpandsForLabel) {
    // Value content with long label should expand width
    Node n;
    n.id = "display1";
    n.name = "Display";
    n.type_name = "ValueDisplay";
    n.input("in");
    n.node_content.type = NodeContentType::Value;
    n.node_content.label = "Temperature in Celsius: ";
    n.node_content.value = 23.5f;

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Should be wide enough for the label text
    // Label is ~25 chars * 9 * 0.6 ≈ 135px + margins
    EXPECT_GT(size.x, 150.0f) << "Node should expand for long value label";
}

TEST(NodeAutoSizeTest, ShortPortNames_CanNarrow) {
    // Node with short names can be narrow
    Node n;
    n.id = "tiny";
    n.name = "X";
    n.type_name = "T";
    n.input("a");
    n.output("b");

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Should be narrow but still fit content
    // Minimum: header text "X" + margins
    EXPECT_LT(size.x, 120.0f) << "Node with short names can be narrow";
    EXPECT_GT(size.x, 40.0f) << "But still has minimum reasonable width";
}

TEST(NodeAutoSizeTest, GaugeWithPorts_HeightIncludesAll) {
    // Gauge height + port rows should all fit
    Node n;
    n.id = "gauge_node";
    n.name = "Meter";
    n.type_name = "Meter";
    n.input("signal");
    n.input("ground");
    n.output("display");
    n.node_content.type = NodeContentType::Gauge;
    n.node_content.min = 0.0f;
    n.node_content.max = 100.0f;
    n.node_content.unit = "%";

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Height = header(24) + 2 port rows(32) + gauge height(~95) + typename(16)
    // Total: ~167px
    EXPECT_GT(size.y, 160.0f) << "Height should fit gauge + ports";
}

TEST(NodeAutoSizeTest, GridSnapping_AutoSize) {
    // Auto-sized nodes should still be grid-snapped
    Node n;
    n.id = "test";
    n.name = "Test";
    n.type_name = "TestComponent";
    n.input("input_port_with_specific_length");

    VisualNode visual(n);

    Pt size = visual.getSize();

    // Should be snapped to 16px grid
    float x_rem = std::fmod(size.x, 16.0f);
    float y_rem = std::fmod(size.y, 16.0f);

    EXPECT_NEAR(x_rem, 0.0f, 0.01f) << "Auto-sized width should be grid-snapped";
    EXPECT_NEAR(y_rem, 0.0f, 0.01f) << "Auto-sized height should be grid-snapped";
}

// ============================================================================
// Regression: setSize(node.size) must not shrink auto-sized nodes
// Root cause: an24_editor.cpp called visual->setSize(node.size) every frame,
// overwriting the auto-sized VisualNode dimensions with stale Node.size (80px),
// causing gauge/switch/value content to render outside the node bounds.
// Fix: remove setSize(node.size) from the editor loop; sync in opposite direction.
// ============================================================================

TEST(NodeAutoSizeTest, Regression_GaugeAutoSize_NotOverwrittenByStaleSize) {
    // Simulate the bug: node loaded from JSON with stale size (120x80),
    // but auto-size should give a larger height for gauges.
    Node n;
    n.id = "voltmeter_2";
    n.name = "V2";
    n.type_name = "Voltmeter";
    n.input("v_in");
    n.output("v_out");
    n.node_content.type = NodeContentType::Gauge;
    n.node_content.min = 0.0f;
    n.node_content.max = 30.0f;
    n.node_content.unit = "V";
    // JSON-loaded size (stale) — size_explicitly_set stays false
    n.size = Pt(120.0f, 80.0f);

    VisualNode visual(n);

    // Auto-size should have expanded node beyond the stale 80px height
    Pt size = visual.getSize();
    EXPECT_GT(size.y, 80.0f)
        << "Gauge node auto-size must expand height beyond stale JSON value";

    // VoltmeterWidget needs ~114px height plus header (~24) + ports (~32) + typename (~16)
    // Total preferred ~ 186px, snapped to grid ~ 192px
    EXPECT_GE(size.y, 176.0f)
        << "Auto-sized height must fit gauge + header + ports";

    // Simulate the FIXED editor loop: sync data model FROM visual
    n.size = visual.getSize();
    EXPECT_GT(n.size.y, 80.0f)
        << "Data model must reflect auto-sized dimensions after sync";
}

TEST(NodeAutoSizeTest, Regression_ContentBoundsInsideNode_Gauge) {
    // All gauge content must render inside node bounds
    Node n;
    n.id = "gauge1";
    n.name = "VMeter";
    n.type_name = "Voltmeter";
    n.input("v_in");
    n.output("v_out");
    n.node_content.type = NodeContentType::Gauge;
    n.node_content.min = 0.0f;
    n.node_content.max = 30.0f;
    n.node_content.unit = "V";

    VisualNode visual(n);

    Bounds cb = visual.getContentBounds();
    Pt pos = visual.getPosition();
    Pt size = visual.getSize();

    // Content bounds must be fully inside node bounds
    EXPECT_GE(cb.x, pos.x)
        << "Content left must not exceed node left";
    EXPECT_GE(cb.y, pos.y)
        << "Content top must not exceed node top";
    EXPECT_LE(cb.x + cb.w, pos.x + size.x)
        << "Content right must not exceed node right";
    EXPECT_LE(cb.y + cb.h, pos.y + size.y)
        << "Content bottom must not exceed node bottom";
}

TEST(NodeAutoSizeTest, Regression_ContentBoundsInsideNode_Switch) {
    Node n;
    n.id = "sw1";
    n.name = "Switch";
    n.type_name = "Switch";
    n.input("ctrl");
    n.output("state");
    n.node_content.type = NodeContentType::Switch;
    n.node_content.label = "PWR";

    VisualNode visual(n);

    Bounds cb = visual.getContentBounds();
    Pt pos = visual.getPosition();
    Pt size = visual.getSize();

    EXPECT_GE(cb.x, pos.x) << "Switch content left within node";
    EXPECT_GE(cb.y, pos.y) << "Switch content top within node";
    EXPECT_LE(cb.x + cb.w, pos.x + size.x) << "Switch content right within node";
    EXPECT_LE(cb.y + cb.h, pos.y + size.y) << "Switch content bottom within node";
}

TEST(NodeAutoSizeTest, Regression_ContentBoundsInsideNode_Value) {
    Node n;
    n.id = "val1";
    n.name = "Display";
    n.type_name = "ValueDisplay";
    n.input("in");
    n.node_content.type = NodeContentType::Value;
    n.node_content.label = "Current: ";
    n.node_content.value = 12.5f;

    VisualNode visual(n);

    Bounds cb = visual.getContentBounds();
    Pt pos = visual.getPosition();
    Pt size = visual.getSize();

    EXPECT_GE(cb.x, pos.x) << "Value content left within node";
    EXPECT_GE(cb.y, pos.y) << "Value content top within node";
    EXPECT_LE(cb.x + cb.w, pos.x + size.x) << "Value content right within node";
    EXPECT_LE(cb.y + cb.h, pos.y + size.y) << "Value content bottom within node";
}

TEST(NodeAutoSizeTest, Regression_CachedVisualPreservesAutoSize) {
    // VisualNodeCache.getOrCreate should preserve auto-sized dimensions
    Node n;
    n.id = "cached_gauge";
    n.name = "V1";
    n.type_name = "Voltmeter";
    n.input("v_in");
    n.output("v_out");
    n.node_content.type = NodeContentType::Gauge;
    n.node_content.min = 0.0f;
    n.node_content.max = 30.0f;
    n.node_content.unit = "V";
    n.size = Pt(120.0f, 80.0f);  // stale JSON size

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(n, {});

    // First call creates the visual — should be auto-sized
    Pt first_size = visual->getSize();
    EXPECT_GT(first_size.y, 80.0f)
        << "First getOrCreate must auto-size beyond stale JSON height";

    // Second call returns the cache hit — must NOT reset to stale size
    auto* visual2 = cache.getOrCreate(n, {});
    Pt second_size = visual2->getSize();
    EXPECT_FLOAT_EQ(first_size.x, second_size.x)
        << "Cached visual width must be stable across getOrCreate calls";
    EXPECT_FLOAT_EQ(first_size.y, second_size.y)
        << "Cached visual height must be stable across getOrCreate calls";
}

// ============================================================================
// Regression: Renaming node must NOT change width (size_explicitly_set on load)
// ============================================================================

TEST(NodeAutoSizeTest, Regression_RenameDoesNotChangeWidth) {
    // Simulate: node loaded from JSON with explicit size, then renamed
    Node n;
    n.id = "azs_1";
    n.name = "AZS_1";
    n.type_name = "AZS";
    n.at(100, 100).size_wh(160, 128);  // size_explicitly_set = true
    n.input("control");
    n.input("v_in");
    n.output("state");
    n.output("v_out");

    VisualNode visual_before(n);
    Pt size_before = visual_before.getSize();

    // Rename to a much longer Cyrillic name
    n.name = "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8 \xd0\x9e\xd1\x81\xd0\xbd\xd0\xbe\xd0\xb2\xd0\xbd\xd1\x8b\xd0\xb5"; // "АЗС Батареи Основные"

    // Cache invalidated → new VisualNode built from same Node data
    VisualNode visual_after(n);
    Pt size_after = visual_after.getSize();

    EXPECT_FLOAT_EQ(size_before.x, size_after.x)
        << "Node width must NOT change after rename when size is explicit";
    EXPECT_FLOAT_EQ(size_before.y, size_after.y)
        << "Node height must NOT change after rename when size is explicit";
}

TEST(NodeAutoSizeTest, EstimateTextWidth_UTF8_CountsCodepoints) {
    // "АЗС" in UTF-8 = 6 bytes, 3 codepoints
    // estimateTextWidth should use codepoints, not bytes
    float width_ascii = HeaderWidget::estimateTextWidth("ABC");
    float width_cyrillic = HeaderWidget::estimateTextWidth("\xd0\x90\xd0\x97\xd0\xa1"); // "АЗС"

    EXPECT_FLOAT_EQ(width_ascii, width_cyrillic)
        << "3 ASCII chars and 3 Cyrillic chars should estimate to the same width";

    // "Hello" = 5 bytes/codepoints, "Привет" = 12 bytes, 6 codepoints
    float w_hello = HeaderWidget::estimateTextWidth("Hello");
    float w_privet = HeaderWidget::estimateTextWidth("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82"); // "Привет"

    EXPECT_NE(w_hello, w_privet) << "5 chars vs 6 chars should differ";
    float expected_ratio = 6.0f / 5.0f;
    EXPECT_FLOAT_EQ(w_privet / w_hello, expected_ratio);
}
