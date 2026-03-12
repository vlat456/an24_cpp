#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/visual/scene/scene.h"
#include "editor/input/canvas_input.h"
#include "editor/visual/scene/wire_manager.h"
#include <fstream>
#include <sstream>
#include <string>

// =============================================================================
// Phase 1: Context Menu Tests — TDD
// =============================================================================

// Helper: blueprint with one node at (0, 0) size 120x80
static Blueprint make_single_node_bp() {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");
    bp.add_node(std::move(n));
    return bp;
}

TEST(ContextMenu, RightClickOnNode_SetsNodeContextMenu) {
    Blueprint bp = make_single_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Node is at (0,0) with size (120, 80). Center is (60, 40).
    // With default viewport (zoom=1, pan=0), screen == world.
    auto result = input.on_mouse_down(Pt(60, 40), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_node_context_menu)
        << "Right-click on a node should set show_node_context_menu";
    EXPECT_EQ(result.context_menu_node_index, 0u)
        << "Should identify node index 0";
    EXPECT_FALSE(result.show_context_menu)
        << "Empty-space context menu should NOT be triggered";
}

TEST(ContextMenu, RightClickOnEmpty_StillShowsAddMenu) {
    Blueprint bp = make_single_node_bp();
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Click far from the node (500, 500) — empty space
    auto result = input.on_mouse_down(Pt(500, 500), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_context_menu)
        << "Right-click on empty space should show add-component menu";
    EXPECT_FALSE(result.show_node_context_menu)
        << "Node context menu should NOT be triggered on empty space";
}

TEST(ContextMenu, RightClickOnSecondNode_ReportsCorrectIndex) {
    Blueprint bp;
    Node n1;
    n1.id = "a";
    n1.type_name = "Battery";
    n1.at(0, 0).size_wh(120, 80);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "b";
    n2.type_name = "Resistor";
    n2.at(300, 0).size_wh(120, 80);
    bp.add_node(std::move(n2));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    Pt canvas_min(0, 0);
    // Click on second node center (360, 40)
    auto result = input.on_mouse_down(Pt(360, 40), MouseButton::Right, canvas_min);

    EXPECT_TRUE(result.show_node_context_menu);
    EXPECT_EQ(result.context_menu_node_index, 1u)
        << "Should identify second node (index 1)";
}

// =============================================================================
// selectNodeById — programmatic selection from Inspector
// =============================================================================

TEST(SelectNodeById, Found_SelectsAndCenters) {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.type_name = "Battery";
    n.at(200, 300).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");
    bp.add_node(std::move(n));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    bool found = input.selectNodeById("bat1");
    EXPECT_TRUE(found);
    EXPECT_EQ(input.selected_nodes().size(), 1u);
    EXPECT_EQ(input.selected_nodes()[0], 0u);
}

TEST(SelectNodeById, NotFound_ReturnsFalse) {
    Blueprint bp;
    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    EXPECT_FALSE(input.selectNodeById("nonexistent"));
    EXPECT_TRUE(input.selected_nodes().empty());
}

TEST(SelectNodeById, ClearsPreviousSelection) {
    Blueprint bp;
    Node n1, n2;
    n1.id = "bat1"; n1.type_name = "Battery"; n1.at(0, 0).size_wh(120, 80);
    n1.input("v_in"); n1.output("v_out");
    n2.id = "bat2"; n2.type_name = "Battery"; n2.at(200, 0).size_wh(120, 80);
    n2.input("v_in"); n2.output("v_out");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    VisualScene scene(bp);
    WireManager wm(scene);
    CanvasInput input(scene, wm);

    input.selectNodeById("bat1");
    EXPECT_EQ(input.selected_nodes().size(), 1u);

    input.selectNodeById("bat2");
    EXPECT_EQ(input.selected_nodes().size(), 1u);
    EXPECT_EQ(input.selected_nodes()[0], 1u);
}

// =============================================================================
// Bus port swap integration tests (VisualScene::swapWirePortsOnBus)
// These verify the full scene-level swap: visual port order + wire re-routing
// =============================================================================

// Helper: build a blueprint with a bus and two source nodes connected to it.
// Returns the blueprint; caller binds the scene.
static Blueprint make_bus_with_two_wires() {
    Blueprint bp;

    // Source node A at (0, 0)
    Node na;
    na.id = "node_a";
    na.type_name = "Battery";
    na.at(0, 100).size_wh(80, 48);
    na.output("out");
    bp.add_node(std::move(na));

    // Source node B at (0, 200)
    Node nb;
    nb.id = "node_b";
    nb.type_name = "Battery";
    nb.at(0, 200).size_wh(80, 48);
    nb.output("out");
    bp.add_node(std::move(nb));

    // Bus at (320, 96) — horizontal, sized for 2 wires + 1 logical port.
    // Positions are exact multiples of PORT_LAYOUT_GRID=16 so snap_to_grid is a no-op.
    Node bus;
    bus.id = "bus1";
    bus.name = "Bus";
    bus.type_name = "Bus";
    bus.render_hint = "bus";
    bus.at(320, 96).size_wh(80, 32);
    bus.input("v");
    bus.output("v");
    bp.add_node(std::move(bus));

    // Wire 1: node_a.out → bus1.v
    Wire w1 = Wire::make("wire_1", wire_output("node_a", "out"), wire_input("bus1", "v"));
    bp.add_wire(std::move(w1));

    // Wire 2: node_b.out → bus1.v
    Wire w2 = Wire::make("wire_2", wire_output("node_b", "out"), wire_input("bus1", "v"));
    bp.add_wire(std::move(w2));

    return bp;
}

// After swapWirePortsOnBus, the two alias ports should have exchanged positions:
// wire_1 now lives at slot 1 and wire_2 at slot 0.
TEST(SwapWirePortsOnBus, SwapSucceeds_PortPositionsExchanged) {
    Blueprint bp = make_bus_with_two_wires();
    VisualScene scene(bp);

    // Force the bus visual node into cache by accessing it
    const Node* bus_node = bp.find_node("bus1");
    ASSERT_NE(bus_node, nullptr);
    auto* bus_vis = scene.cache().getOrCreate(*bus_node, bp.wires);
    ASSERT_NE(bus_vis, nullptr);

    // Record alias port positions BEFORE swap
    Pt pos_wire1_before = scene.portPosition(*bus_node, "v", "wire_1");
    Pt pos_wire2_before = scene.portPosition(*bus_node, "v", "wire_2");

    // Ports should start at different positions
    EXPECT_NE(pos_wire1_before.x, pos_wire2_before.x);

    // Perform swap using correct wire IDs (not port names like "v")
    bool ok = scene.swapWirePortsOnBus("bus1", "wire_2", "wire_1");
    EXPECT_TRUE(ok) << "swapWirePortsOnBus should succeed with valid wire IDs";

    // After swap: wire_1 should be at slot 1 (old slot of wire_2) and vice versa
    Pt pos_wire1_after = scene.portPosition(*bus_node, "v", "wire_1");
    Pt pos_wire2_after = scene.portPosition(*bus_node, "v", "wire_2");

    EXPECT_FLOAT_EQ(pos_wire1_after.x, pos_wire2_before.x)
        << "wire_1 should now occupy wire_2's old slot";
    EXPECT_FLOAT_EQ(pos_wire1_after.y, pos_wire2_before.y);
    EXPECT_FLOAT_EQ(pos_wire2_after.x, pos_wire1_before.x)
        << "wire_2 should now occupy wire_1's old slot";
    EXPECT_FLOAT_EQ(pos_wire2_after.y, pos_wire1_before.y);
}

// Passing the logical port name "v" as wire_id_b (the old broken argument) must fail.
// This regression test documents the root cause and guards against reintroducing the bug.
TEST(SwapWirePortsOnBus, PassingLogicalPortName_ReturnsFalse) {
    Blueprint bp = make_bus_with_two_wires();
    VisualScene scene(bp);

    // Warm up cache
    const Node* bus_node = bp.find_node("bus1");
    ASSERT_NE(bus_node, nullptr);
    scene.cache().getOrCreate(*bus_node, bp.wires);

    // "v" is a port name, not a wire ID — no wire has id="v"
    bool ok = scene.swapWirePortsOnBus("bus1", "wire_2", "v");
    EXPECT_FALSE(ok)
        << "swapWirePortsOnBus must return false when wire_id_b is 'v' (a port name, not a wire ID)";
}

// After a successful swap, routing points are preserved. Endpoints are resolved
// every frame via resolveWirePort(), so wires automatically follow their ports.
TEST(SwapWirePortsOnBus, AfterSwap_RoutingPointsPreserved) {
    Blueprint bp = make_bus_with_two_wires();
    VisualScene scene(bp);

    // Pre-seed hand-crafted routing points
    bp.wires[0].routing_points.push_back(Pt(99, 99));
    bp.wires[1].routing_points.push_back(Pt(88, 88));

    const Node* bus_node = bp.find_node("bus1");
    ASSERT_NE(bus_node, nullptr);
    scene.cache().getOrCreate(*bus_node, bp.wires);

    bool ok = scene.swapWirePortsOnBus("bus1", "wire_2", "wire_1");
    ASSERT_TRUE(ok);

    // swapWirePortsOnBus swaps the complete wire objects (including routing
    // points) so each wire's geometry travels with it to the new bus slot.
    // After swap: wires[0] is the old wire_2 (had {88,88}), wires[1] is old wire_1 (had {99,99}).
    ASSERT_EQ(bp.wires[0].routing_points.size(), 1u);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].x, 88);
    EXPECT_FLOAT_EQ(bp.wires[0].routing_points[0].y, 88);

    ASSERT_EQ(bp.wires[1].routing_points.size(), 1u);
    EXPECT_FLOAT_EQ(bp.wires[1].routing_points[0].x, 99);
    EXPECT_FLOAT_EQ(bp.wires[1].routing_points[0].y, 99);
}

// =============================================================================
// Regression: Context menu popup lifecycle (ImGui OpenPopup/BeginPopup pattern)
// =============================================================================
//
// Bug: When extracting popup code into ContextMenus class, an early return
// was introduced that broke ImGui's popup lifecycle:
//
//   BROKEN:  if (!ws.contextMenu.show) return;  // kills BeginPopup on frame N+1
//            ImGui::OpenPopup("AddComponent");
//            ws.contextMenu.show = false;
//            if (!ImGui::BeginPopup("AddComponent")) return;
//
//   CORRECT: if (ws.contextMenu.show) {
//                ImGui::OpenPopup("AddComponent");   // one-shot trigger
//                ws.contextMenu.show = false;
//            }
//            if (!ImGui::BeginPopup("AddComponent")) return;  // must run EVERY frame
//
// Root cause: ImGui::OpenPopup() fires on frame N, but BeginPopup() must be
// called on EVERY subsequent frame to keep the popup alive. An early return
// on !show prevents BeginPopup from running on frames N+1, N+2, etc.
//
// These tests read the source file at test time and verify the correct pattern
// is present, catching future refactors that might reintroduce the bug.
// =============================================================================

/// Read file contents as a string. Returns empty string on failure.
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Check that no line matches "if (!ws.*.show) return;" which would gate
/// BeginPopup behind the show flag (the exact bug pattern).
static bool has_early_return_on_show_flag(const std::string& src) {
    // Look for the broken pattern: early return when show is false,
    // placed before BeginPopup. The regex-like check is:
    //   "if (!ws." ... ".show)" ... "return"  on same logical line
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip comment lines
        auto trimmed = line;
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
            trimmed.erase(trimmed.begin());
        if (trimmed.substr(0, 2) == "//") continue;

        // Check for the anti-pattern: "if (!ws.*show) return"
        if (line.find("!ws.") != std::string::npos &&
            line.find(".show") != std::string::npos &&
            line.find("return") != std::string::npos) {
            return true;  // found the broken pattern
        }
    }
    return false;
}

/// Verify that OpenPopup is gated by the show flag (inside an if block)
/// but BeginPopup is NOT inside that same if block.
static bool has_correct_popup_pattern(const std::string& src,
                                       const std::string& popup_name) {
    // The correct pattern has:
    // 1. "if (ws.*show)" followed by "OpenPopup" (within ~3 lines)
    // 2. ".show = false" (within the gated block)
    // 3. "BeginPopup" OUTSIDE the gated block (at lower indentation or after closing brace)

    bool found_gated_open = false;
    bool found_begin_outside = false;

    std::istringstream stream(src);
    std::string line;
    bool inside_show_block = false;
    int brace_depth = 0;

    while (std::getline(stream, line)) {
        // Detect entry into the show-gated block
        if (!inside_show_block &&
            line.find(".show") != std::string::npos &&
            line.find("if") != std::string::npos &&
            line.find("!") == std::string::npos &&   // NOT negated
            line.find("OpenPopup") == std::string::npos) {
            // Next few lines should have OpenPopup
            inside_show_block = true;
            brace_depth = 0;
            for (char c : line) {
                if (c == '{') brace_depth++;
                if (c == '}') brace_depth--;
            }
            continue;
        }

        if (inside_show_block) {
            for (char c : line) {
                if (c == '{') brace_depth++;
                if (c == '}') brace_depth--;
            }

            if (line.find("OpenPopup") != std::string::npos &&
                line.find(popup_name) != std::string::npos) {
                found_gated_open = true;
            }

            // BeginPopup should NOT be inside this block
            if (line.find("BeginPopup") != std::string::npos) {
                return false;  // BeginPopup is inside the show-gated block = BUG
            }

            if (brace_depth <= 0) {
                inside_show_block = false;
            }
            continue;
        }

        // Outside the show block: BeginPopup should appear here
        if (line.find("BeginPopup") != std::string::npos &&
            line.find(popup_name) != std::string::npos) {
            found_begin_outside = true;
        }
    }

    return found_gated_open && found_begin_outside;
}

TEST(ContextMenuRegression, NoEarlyReturnOnShowFlag) {
    std::string src = read_file(TEST_DATA_DIR "/src/editor/visual/popups/context_menus.cpp");
    ASSERT_FALSE(src.empty())
        << "Could not read context_menus.cpp — check TEST_DATA_DIR";

    EXPECT_FALSE(has_early_return_on_show_flag(src))
        << "REGRESSION: context_menus.cpp contains 'if (!ws.*.show) return;' pattern.\n"
           "This breaks ImGui popup lifecycle: BeginPopup must be called EVERY frame,\n"
           "not only when show==true. OpenPopup is a one-shot trigger; BeginPopup keeps\n"
           "the popup alive on subsequent frames. See commit c42bcb8 for the original bug.";
}

TEST(ContextMenuRegression, AddComponentPopup_CorrectLifecyclePattern) {
    std::string src = read_file(TEST_DATA_DIR "/src/editor/visual/popups/context_menus.cpp");
    ASSERT_FALSE(src.empty());

    EXPECT_TRUE(has_correct_popup_pattern(src, "AddComponent"))
        << "REGRESSION: AddComponent popup does not follow the correct pattern.\n"
           "OpenPopup(\"AddComponent\") must be inside 'if (ws.contextMenu.show) { ... }'\n"
           "but BeginPopup(\"AddComponent\") must be OUTSIDE that block (called every frame).";
}

TEST(ContextMenuRegression, NodeContextPopup_CorrectLifecyclePattern) {
    std::string src = read_file(TEST_DATA_DIR "/src/editor/visual/popups/context_menus.cpp");
    ASSERT_FALSE(src.empty());

    EXPECT_TRUE(has_correct_popup_pattern(src, "NodeContextMenu"))
        << "REGRESSION: NodeContextMenu popup does not follow the correct pattern.\n"
           "OpenPopup(\"NodeContextMenu\") must be inside 'if (ws.nodeContextMenu.show) { ... }'\n"
           "but BeginPopup(\"NodeContextMenu\") must be OUTSIDE that block (called every frame).";
}
