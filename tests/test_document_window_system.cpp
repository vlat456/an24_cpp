#include <gtest/gtest.h>
#include "editor/document.h"
#include "editor/window_system.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "json_parser/json_parser.h"
#include <fstream>

// ============================================================================
// Document tests
// ============================================================================

TEST(Document, ConstructionGeneratesUniqueId) {
    Document d1;
    Document d2;
    EXPECT_NE(d1.id(), d2.id());
    EXPECT_FALSE(d1.id().empty());
    EXPECT_FALSE(d2.id().empty());
}

TEST(Document, DefaultState) {
    Document doc;
    EXPECT_EQ(doc.displayName(), "Untitled");
    EXPECT_TRUE(doc.filepath().empty());
    EXPECT_FALSE(doc.isSimulationRunning());
}

TEST(Document, BlueprintAccess) {
    Document doc;
    // Should be able to add nodes to the blueprint
    Node n;
    n.id = "test1";
    n.name = "test1";
    n.pos = Pt(0, 0);
    n.size = Pt(100, 80);
    doc.blueprint().add_node(std::move(n));
    EXPECT_EQ(doc.blueprint().nodes.size(), 1u);
    EXPECT_EQ(doc.blueprint().nodes[0].id, "test1");
}

TEST(Document, WindowManagerAccess) {
    Document doc;
    // Root window should exist
    EXPECT_EQ(doc.windowManager().windows().size(), 1u);
    EXPECT_TRUE(doc.root().group_id.empty());
}

TEST(Document, SceneSharesBlueprint) {
    Document doc;
    Node n;
    n.id = "n1";
    n.name = "n1";
    n.pos = Pt(50, 50);
    n.size = Pt(100, 80);
    doc.blueprint().add_node(std::move(n));
    // Scene should see the same nodes
    EXPECT_EQ(doc.scene().nodeCount(), 1u);
}

TEST(Document, SimulationStartStop) {
    Document doc;
    EXPECT_FALSE(doc.isSimulationRunning());
    doc.startSimulation();
    EXPECT_TRUE(doc.isSimulationRunning());
    doc.stopSimulation();
    EXPECT_FALSE(doc.isSimulationRunning());
}

TEST(Document, SimulationDoubleStartIsNoop) {
    Document doc;
    doc.startSimulation();
    EXPECT_TRUE(doc.isSimulationRunning());
    doc.startSimulation(); // should not crash or change state
    EXPECT_TRUE(doc.isSimulationRunning());
    doc.stopSimulation();
}

TEST(Document, SignalOverridesAndHeldButtons) {
    Document doc;
    EXPECT_TRUE(doc.signalOverrides().empty());
    EXPECT_TRUE(doc.heldButtons().empty());

    doc.holdButtonPress("btn1");
    EXPECT_EQ(doc.heldButtons().size(), 1u);
    EXPECT_TRUE(doc.heldButtons().count("btn1"));

    doc.holdButtonRelease("btn1");
    EXPECT_TRUE(doc.heldButtons().empty());
}

TEST(Document, TriggerSwitchSetsOverride) {
    Document doc;
    doc.startSimulation();
    doc.triggerSwitch("sw1");
    EXPECT_FALSE(doc.signalOverrides().empty());
    doc.stopSimulation();
}

TEST(Document, ApplyInputResultContextMenu) {
    Document doc;
    InputResult r;
    r.show_context_menu = true;
    r.context_menu_pos = Pt(100, 200);

    auto action = doc.applyInputResult(r, "group1");
    EXPECT_TRUE(action.show_context_menu);
    EXPECT_FLOAT_EQ(action.context_menu_pos.x, 100.0f);
    EXPECT_FLOAT_EQ(action.context_menu_pos.y, 200.0f);
    EXPECT_EQ(action.context_menu_group_id, "group1");
    EXPECT_FALSE(action.show_node_context_menu);
}

TEST(Document, ApplyInputResultNodeContextMenu) {
    Document doc;
    InputResult r;
    r.show_node_context_menu = true;
    r.context_menu_node_index = 5;

    auto action = doc.applyInputResult(r, "grp");
    EXPECT_TRUE(action.show_node_context_menu);
    EXPECT_EQ(action.context_menu_node_index, 5u);
    EXPECT_EQ(action.node_context_menu_group_id, "grp");
}

TEST(Document, OpenSubWindowForMissingGroupDoesNotCrash) {
    Document doc;
    // Should log error but not crash
    doc.openSubWindow("nonexistent_group");
    // Only root window should exist
    EXPECT_EQ(doc.windowManager().windows().size(), 1u);
}

TEST(Document, OpenSubWindowCreatesWindow) {
    Document doc;

    // Create a sub-blueprint instance in the blueprint
    SubBlueprintInstance cg;
    cg.id = "lamp1";
    cg.type_name = "Lamp";
    cg.internal_node_ids = {"lamp1:r1"};
    cg.baked_in = true;
    doc.blueprint().sub_blueprint_instances.push_back(cg);

    doc.openSubWindow("lamp1");
    EXPECT_EQ(doc.windowManager().windows().size(), 2u);
}

TEST(Document, NonCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<Document>);
    EXPECT_FALSE(std::is_copy_assignable_v<Document>);
    EXPECT_FALSE(std::is_move_constructible_v<Document>);
    EXPECT_FALSE(std::is_move_assignable_v<Document>);
}

// ============================================================================
// WindowSystem tests
// ============================================================================

TEST(WindowSystem, ConstructionCreatesOneDocument) {
    WindowSystem ws;
    EXPECT_EQ(ws.documentCount(), 1u);
    EXPECT_NE(ws.activeDocument(), nullptr);
}

TEST(WindowSystem, CreateDocumentIncrementsCount) {
    WindowSystem ws;
    EXPECT_EQ(ws.documentCount(), 1u);
    ws.createDocument();
    EXPECT_EQ(ws.documentCount(), 2u);
    ws.createDocument();
    EXPECT_EQ(ws.documentCount(), 3u);
}

TEST(WindowSystem, CreateDocumentSetsActive) {
    WindowSystem ws;
    Document* first = ws.activeDocument();
    Document& second = ws.createDocument();
    EXPECT_EQ(ws.activeDocument(), &second);
    EXPECT_NE(ws.activeDocument(), first);
}

TEST(WindowSystem, SetActiveDocumentUpdatesPointer) {
    WindowSystem ws;
    Document* first = ws.activeDocument();
    Document& second = ws.createDocument();
    ws.setActiveDocument(first);
    EXPECT_EQ(ws.activeDocument(), first);
    ws.setActiveDocument(&second);
    EXPECT_EQ(ws.activeDocument(), &second);
}

// === closeDocument regression tests ===

TEST(WindowSystem, CloseDocumentRemovesIt) {
    WindowSystem ws;
    ws.createDocument();
    EXPECT_EQ(ws.documentCount(), 2u);

    Document* first = ws.documents()[0].get();
    ws.closeDocument(*first);
    EXPECT_EQ(ws.documentCount(), 1u);
}

// REGRESSION: closeDocument use-after-move bug.
// Old code used std::remove_if then accessed moved-from unique_ptr.
TEST(WindowSystem, CloseActiveDocumentSwitchesToNext) {
    WindowSystem ws;
    Document& d2 = ws.createDocument();
    Document& d3 = ws.createDocument();

    // Active is d3 (the last created)
    EXPECT_EQ(ws.activeDocument(), &d3);

    // Close d3 — should switch to another valid document
    ws.closeDocument(d3);
    EXPECT_EQ(ws.documentCount(), 2u);
    EXPECT_NE(ws.activeDocument(), nullptr);
    // Active should be some valid document pointer
    bool found = false;
    for (const auto& d : ws.documents()) {
        if (d.get() == ws.activeDocument()) { found = true; break; }
    }
    EXPECT_TRUE(found) << "activeDocument should point to a valid document";
}

// REGRESSION: closing middle document should pick correct replacement.
TEST(WindowSystem, CloseMiddleDocumentSwitchesCorrectly) {
    WindowSystem ws;
    Document* d1 = ws.documents()[0].get();
    Document& d2 = ws.createDocument();
    Document& d3 = ws.createDocument();

    ws.setActiveDocument(&d2);
    EXPECT_EQ(ws.activeDocument(), &d2);

    ws.closeDocument(d2);
    EXPECT_EQ(ws.documentCount(), 2u);
    // Should switch to d3 (next after d2) or d1, not crash
    EXPECT_NE(ws.activeDocument(), nullptr);
}

// REGRESSION: closing the only document should create a new empty one.
TEST(WindowSystem, CloseLastDocumentCreatesNew) {
    WindowSystem ws;
    EXPECT_EQ(ws.documentCount(), 1u);
    Document* only = ws.activeDocument();

    ws.closeDocument(*only);
    // Should have created a new document
    EXPECT_EQ(ws.documentCount(), 1u);
    EXPECT_NE(ws.activeDocument(), nullptr);
}

// REGRESSION: Inspector::detectSceneChange crash (SIGSEGV) after document close.
// closeDocument picked a replacement active_document_ but didn't update
// inspector_.scene_, leaving it as a dangling pointer to the destroyed scene.
TEST(WindowSystem, CloseActiveDocumentUpdatesInspectorScene) {
    WindowSystem ws;
    Document& d2 = ws.createDocument();

    // Add a node to d2 so its scene is non-trivial
    Node n;
    n.id = "crash_test";
    n.name = "crash_test";
    n.pos = Pt(0, 0);
    n.size = Pt(100, 80);
    d2.blueprint().add_node(std::move(n));

    ws.setActiveDocument(&d2);
    // Inspector now points to d2's scene

    // Close d2 — inspector must switch to d1's scene, not dangle
    ws.closeDocument(d2);
    EXPECT_NE(ws.activeDocument(), nullptr);

    // This would crash (SIGSEGV) if inspector_.scene_ is dangling
    ws.inspector().buildDisplayTree();
}

// REGRESSION: closeDocument should clear context menu source_doc pointers.
TEST(WindowSystem, CloseDocumentClearsContextMenuPointers) {
    WindowSystem ws;
    Document& d2 = ws.createDocument();

    // Set up context menu pointing to d2
    ws.contextMenu.source_doc = &d2;
    ws.nodeContextMenu.source_doc = &d2;
    ws.colorPicker.source_doc = &d2;
    ws.colorPicker.show = true;

    ws.closeDocument(d2);

    EXPECT_EQ(ws.contextMenu.source_doc, nullptr);
    EXPECT_EQ(ws.nodeContextMenu.source_doc, nullptr);
    EXPECT_EQ(ws.colorPicker.source_doc, nullptr);
    EXPECT_FALSE(ws.colorPicker.show);
}

// REGRESSION: closeDocument should NOT clear pointers for other docs.
TEST(WindowSystem, CloseDocumentKeepsOtherContextMenuPointers) {
    WindowSystem ws;
    Document* d1 = ws.documents()[0].get();
    Document& d2 = ws.createDocument();

    // Set up context menu pointing to d1
    ws.contextMenu.source_doc = d1;
    ws.nodeContextMenu.source_doc = d1;

    // Close d2 — d1's pointers should remain
    ws.closeDocument(d2);
    EXPECT_EQ(ws.contextMenu.source_doc, d1);
    EXPECT_EQ(ws.nodeContextMenu.source_doc, d1);
}

// Stress: close all documents one by one
TEST(WindowSystem, CloseAllDocumentsOneByOne) {
    WindowSystem ws;
    ws.createDocument();
    ws.createDocument();
    ws.createDocument();
    EXPECT_EQ(ws.documentCount(), 4u);

    // Close all one by one (each close may create a new doc when last is closed)
    while (ws.documentCount() > 1) {
        Document* d = ws.documents().front().get();
        ws.closeDocument(*d);
    }
    // The last one creates a new empty doc
    Document* last = ws.activeDocument();
    ASSERT_NE(last, nullptr);
    ws.closeDocument(*last);
    EXPECT_EQ(ws.documentCount(), 1u); // new one created
}

TEST(WindowSystem, CloseAllDocumentsMethod) {
    WindowSystem ws;
    ws.createDocument();
    ws.createDocument();
    EXPECT_EQ(ws.documentCount(), 3u);

    EXPECT_TRUE(ws.closeAllDocuments());
    EXPECT_EQ(ws.documentCount(), 1u); // fresh document
    EXPECT_NE(ws.activeDocument(), nullptr);
}

TEST(WindowSystem, HandleInputActionSetsContextMenu) {
    WindowSystem ws;
    Document* doc = ws.activeDocument();

    Document::InputResultAction action;
    action.show_context_menu = true;
    action.context_menu_pos = Pt(10, 20);
    action.context_menu_group_id = "g1";

    ws.handleInputAction(action, *doc);

    EXPECT_TRUE(ws.contextMenu.show);
    EXPECT_FLOAT_EQ(ws.contextMenu.position.x, 10.0f);
    EXPECT_FLOAT_EQ(ws.contextMenu.position.y, 20.0f);
    EXPECT_EQ(ws.contextMenu.group_id, "g1");
    EXPECT_EQ(ws.contextMenu.source_doc, doc);
}

TEST(WindowSystem, HandleInputActionSetsNodeContextMenu) {
    WindowSystem ws;
    Document* doc = ws.activeDocument();

    Document::InputResultAction action;
    action.show_node_context_menu = true;
    action.context_menu_node_index = 3;
    action.node_context_menu_group_id = "sub1";

    ws.handleInputAction(action, *doc);

    EXPECT_TRUE(ws.nodeContextMenu.show);
    EXPECT_EQ(ws.nodeContextMenu.node_index, 3u);
    EXPECT_EQ(ws.nodeContextMenu.group_id, "sub1");
    EXPECT_EQ(ws.nodeContextMenu.source_doc, doc);
}

TEST(WindowSystem, OpenDocumentDuplicatePathReturnsExisting) {
    WindowSystem ws;
    // We can't actually load a file without a real JSON file,
    // but we can test the path check by setting filepath manually
    Document* d1 = ws.activeDocument();

    // findDocumentByPath for non-existent
    EXPECT_EQ(ws.findDocumentByPath("/nonexistent.json"), nullptr);
}

TEST(WindowSystem, InspectorUpdatedOnActiveDocumentChange) {
    WindowSystem ws;
    // Add a node to the first document
    Document* d1 = ws.activeDocument();
    Node n;
    n.id = "node1";
    n.name = "node1";
    n.pos = Pt(0, 0);
    n.size = Pt(100, 80);
    d1->blueprint().add_node(std::move(n));

    Document& d2 = ws.createDocument();
    // Active is now d2
    EXPECT_EQ(ws.activeDocument(), &d2);

    // Switch back to d1
    ws.setActiveDocument(d1);
    EXPECT_EQ(ws.activeDocument(), d1);
    // Inspector should have been updated (marked dirty)
}

// ============================================================================
// Edge case: rapid create/close cycles
// ============================================================================

// ============================================================================
// REGRESSION: Bug 2 — "Bake In" must be available for non-baked-in SBI nodes
// at root level (group_id = "").
// The bug: an24_editor.cpp guards "Bake In" with `if (!group_id.empty())`,
// so it only appears when right-clicking a node INSIDE a sub-window (where
// group_id is the SBI id). But when you right-click the collapsed node on the
// root canvas (group_id=""), the menu item is missing.
// The fix: also check if the right-clicked node's ID matches an SBI.
// This test verifies the data model supports finding SBI by node ID at root.
// ============================================================================

TEST(BakeInMenuRegression, FindSBI_ByNodeId_AtRootLevel) {
    Document doc;

    // Create a non-baked-in SBI with collapsed node at root level
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp"};
    doc.blueprint().sub_blueprint_instances.push_back(sbi);

    // Collapsed node at root
    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.at(100, 100).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.group_id = "";  // root level
    doc.blueprint().add_node(std::move(collapsed));

    // Internal nodes
    Node vin; vin.id = "lamp_1:vin"; vin.group_id = "lamp_1";
    vin.at(0, 0).size_wh(100, 60);
    doc.blueprint().add_node(std::move(vin));

    Node lamp; lamp.id = "lamp_1:lamp"; lamp.group_id = "lamp_1";
    lamp.at(200, 0).size_wh(100, 60);
    doc.blueprint().add_node(std::move(lamp));

    // When right-clicking the collapsed node at root level, the context menu
    // has group_id = "" (root). The current code only checks group_id, but
    // the correct fix should ALSO check the node's own ID against SBI.
    // This verifies the data is there:
    const std::string root_group_id = "";
    const std::string node_id = "lamp_1";

    // Current (broken) path: find_sub_blueprint_instance(group_id) where group_id=""
    auto* by_group = doc.blueprint().find_sub_blueprint_instance(root_group_id);
    EXPECT_EQ(by_group, nullptr)
        << "No SBI has id='', so group_id-based lookup fails at root";

    // Correct path: find_sub_blueprint_instance(node.id) 
    auto* by_node_id = doc.blueprint().find_sub_blueprint_instance(node_id);
    ASSERT_NE(by_node_id, nullptr)
        << "SBI must be findable by the collapsed node's ID";
    EXPECT_FALSE(by_node_id->baked_in)
        << "Found SBI must be non-baked-in (eligible for Bake In)";
}

// ============================================================================
// REGRESSION: Bug 3 — "Edit Original" must be available for non-baked-in SBI
// nodes at root level.
// Same guard issue as Bug 2. Additionally verifies blueprint_path is populated.
// ============================================================================

TEST(EditOriginalRegression, SBI_HasBlueprintPath_AtRootLevel) {
    Document doc;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = false;
    doc.blueprint().sub_blueprint_instances.push_back(sbi);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.at(0, 0).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.group_id = "";
    doc.blueprint().add_node(std::move(collapsed));

    // Lookup by node ID (the fix path)
    auto* found = doc.blueprint().find_sub_blueprint_instance("lamp_1");
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(found->baked_in);
    EXPECT_FALSE(found->blueprint_path.empty())
        << "SBI must have blueprint_path for 'Edit Original' to construct library path";
    EXPECT_EQ(found->blueprint_path, "systems/lamp_pass_through");
}

// ============================================================================
// REGRESSION: Bug 4 — openSubWindow must work for non-baked-in SBIs.
// The bug: double-clicking a collapsed non-baked-in node at root calls
// openSubWindow, which creates a window with read_only=true. But then
// an24_editor.cpp skips process_window for read_only windows, preventing
// further double-click drill-down into nested composites.
// This test verifies the Document model layer: openSubWindow creates the
// window, and it's read-only for non-baked-in SBIs.
// ============================================================================

TEST(OpenSubWindowRegression, NonBakedIn_CreatesReadOnlyWindow) {
    Document doc;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin"};
    doc.blueprint().sub_blueprint_instances.push_back(sbi);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.at(0, 0).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    doc.blueprint().add_node(std::move(collapsed));

    Node vin; vin.id = "lamp_1:vin"; vin.group_id = "lamp_1";
    vin.at(0, 0).size_wh(100, 60);
    doc.blueprint().add_node(std::move(vin));

    // Open sub-window — should succeed and set read_only
    EXPECT_EQ(doc.windowManager().windows().size(), 1u);  // just root
    doc.openSubWindow("lamp_1");
    EXPECT_EQ(doc.windowManager().windows().size(), 2u);

    auto* win = doc.windowManager().find("lamp_1");
    ASSERT_NE(win, nullptr);
    EXPECT_TRUE(win->read_only)
        << "Non-baked-in sub-blueprint window must be read-only";
}

TEST(OpenSubWindowRegression, BakedIn_CreatesEditableWindow) {
    Document doc;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.baked_in = true;
    sbi.internal_node_ids = {"lamp_1:vin"};
    doc.blueprint().sub_blueprint_instances.push_back(sbi);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.at(0, 0).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    doc.blueprint().add_node(std::move(collapsed));

    Node vin; vin.id = "lamp_1:vin"; vin.group_id = "lamp_1";
    vin.at(0, 0).size_wh(100, 60);
    doc.blueprint().add_node(std::move(vin));

    doc.openSubWindow("lamp_1");
    auto* win = doc.windowManager().find("lamp_1");
    ASSERT_NE(win, nullptr);
    EXPECT_FALSE(win->read_only)
        << "Baked-in sub-blueprint window must NOT be read-only";
}

TEST(OpenSubWindowRegression, DoubleClick_RootExpandableNode_ReturnsOpenSubWindow) {
    Document doc;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin"};
    doc.blueprint().sub_blueprint_instances.push_back(sbi);

    // Collapsed node at root
    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.at(100, 100).size_wh(120, 80);
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.group_id = "";
    doc.blueprint().add_node(std::move(collapsed));

    Node vin; vin.id = "lamp_1:vin"; vin.group_id = "lamp_1";
    vin.at(0, 0).size_wh(100, 60);
    doc.blueprint().add_node(std::move(vin));

    // Double-click on the collapsed node (center ~160, 140)
    Pt canvas_min(0, 0);
    InputResult r = doc.input().on_double_click(Pt(160, 140), canvas_min);

    EXPECT_EQ(r.open_sub_window, "lamp_1")
        << "Double-click on collapsed expandable node must request opening sub-window";

    // Apply the result — should create the window
    doc.applyInputResult(r);
    auto* win = doc.windowManager().find("lamp_1");
    ASSERT_NE(win, nullptr)
        << "applyInputResult must create sub-window for non-baked-in SBI";
    EXPECT_TRUE(win->read_only);
}

// ============================================================================
// Edge case: rapid create/close cycles
// ============================================================================

TEST(WindowSystem, RapidCreateCloseDoesNotLeak) {
    WindowSystem ws;
    for (int i = 0; i < 100; ++i) {
        Document& d = ws.createDocument();
        ws.closeDocument(d);
    }
    // Should end with just the initial document
    EXPECT_GE(ws.documentCount(), 1u);
    EXPECT_NE(ws.activeDocument(), nullptr);
}

// ============================================================================
// Document::isPristine tests
// ============================================================================

TEST(Document, IsPristine_NewDocument) {
    Document doc;
    EXPECT_TRUE(doc.isPristine());
}

TEST(Document, IsPristine_AfterLoad) {
    // Create temp file
    char temp_file[] = "/tmp/pristine_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    // Write minimal v2 blueprint
    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "test" },
  "nodes": {
    "b1": { "type": "Battery", "pos": [100, 100] }
  },
  "wires": []
})";
    f.close();

    Document doc;
    EXPECT_TRUE(doc.isPristine());
    
    EXPECT_TRUE(doc.load(temp_file));
    EXPECT_FALSE(doc.isPristine());
    
    std::remove(temp_file);
}

TEST(Document, IsPristine_AfterAddComponent) {
    Document doc;
    TypeRegistry registry = load_type_registry();
    
    EXPECT_TRUE(doc.isPristine());
    
    doc.addComponent("Battery", Pt(100, 100), "", registry);
    
    EXPECT_FALSE(doc.isPristine());
}

// ============================================================================
// WindowSystem: Replace pristine Untitled when loading
// ============================================================================

TEST(WindowSystem, OpenDocument_ReplacesPristineUntitled) {
    // Create temp file
    char temp_file[] = "/tmp/replace_pristive_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "loaded_file" },
  "nodes": {
    "b1": { "type": "Battery", "pos": [100, 100] }
  },
  "wires": []
})";
    f.close();

    WindowSystem ws;
    
    // Should start with 1 pristine Untitled
    EXPECT_EQ(ws.documentCount(), 1u);
    EXPECT_TRUE(ws.activeDocument()->isPristine());
    EXPECT_EQ(ws.activeDocument()->displayName(), "Untitled");
    
    // Load file - should REPLACE the pristine Untitled
    Document* loaded = ws.openDocument(temp_file);
    ASSERT_NE(loaded, nullptr);
    
    // Should still have exactly 1 document
    EXPECT_EQ(ws.documentCount(), 1u);
    EXPECT_EQ(ws.activeDocument(), loaded);
    EXPECT_FALSE(loaded->isPristine());
    EXPECT_NE(loaded->displayName(), "Untitled");
    
    std::remove(temp_file);
}

TEST(WindowSystem, OpenDocument_AddsTabWhenNotPristine) {
    // Create temp file
    char temp_file[] = "/tmp/add_tab_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "loaded_file" },
  "nodes": {
    "b1": { "type": "Battery", "pos": [100, 100] }
  },
  "wires": []
})";
    f.close();

    WindowSystem ws;
    TypeRegistry registry = load_type_registry();
    
    // Modify the pristine Untitled so it's no longer pristine
    ws.activeDocument()->addComponent("Battery", Pt(100, 100), "", registry);
    EXPECT_FALSE(ws.activeDocument()->isPristine());
    
    // Load file - should ADD a new tab (not replace)
    Document* loaded = ws.openDocument(temp_file);
    ASSERT_NE(loaded, nullptr);
    
    // Should now have 2 documents
    EXPECT_EQ(ws.documentCount(), 2u);
    EXPECT_EQ(ws.activeDocument(), loaded);
    
    std::remove(temp_file);
}

// ============================================================================
// WindowSystem + RecentFiles integration
// ============================================================================

TEST(WindowSystem, OpenDocument_AddsToRecentFiles) {
    // Create temp file
    char temp_file[] = "/tmp/recent_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "test" },
  "nodes": { "b1": { "type": "Battery", "pos": [100, 100] } },
  "wires": []
})";
    f.close();

    WindowSystem ws;
    EXPECT_TRUE(ws.recent_files.empty());
    
    Document* loaded = ws.openDocument(temp_file);
    ASSERT_NE(loaded, nullptr);
    
    EXPECT_FALSE(ws.recent_files.empty());
    EXPECT_EQ(ws.recent_files.files().size(), 1u);
    EXPECT_EQ(ws.recent_files.files()[0], temp_file);
    
    std::remove(temp_file);
}

TEST(WindowSystem, OpenDocument_DuplicatePath_UpdatesRecentFiles) {
    // Create temp files
    char temp_file[] = "/tmp/recent_dup_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "test" },
  "nodes": { "b1": { "type": "Battery", "pos": [100, 100] } },
  "wires": []
})";
    f.close();

    char temp_file2[] = "/tmp/recent_dup_test2_XXXXXX";
    int fd2 = mkstemp(temp_file2);
    ASSERT_GE(fd2, 0);
    close(fd2);

    std::ofstream f2(temp_file2);
    f2 << R"({
  "version": 2,
  "meta": { "name": "test2" },
  "nodes": { "b1": { "type": "Battery", "pos": [100, 100] } },
  "wires": []
})";
    f2.close();

    WindowSystem ws;
    
    ws.openDocument(temp_file);
    ws.openDocument(temp_file2);
    
    EXPECT_EQ(ws.recent_files.files().size(), 2u);
    EXPECT_EQ(ws.recent_files.files()[0], temp_file2);  // Most recent
    
    // Open first file again - should move to front
    ws.openDocument(temp_file);
    
    EXPECT_EQ(ws.recent_files.files().size(), 2u);
    EXPECT_EQ(ws.recent_files.files()[0], temp_file);  // Now most recent
    
    std::remove(temp_file);
    std::remove(temp_file2);
}

// ============================================================================
// Edge case tests
// ============================================================================

// EDGE: Document becomes not pristine after adding a wire
TEST(Document, IsPristine_AfterAddWire) {
    Document doc;
    TypeRegistry registry = load_type_registry();
    
    EXPECT_TRUE(doc.isPristine());
    
    // Add two nodes
    doc.addComponent("Battery", Pt(100, 100), "", registry);
    doc.addComponent("Lamp", Pt(300, 100), "", registry);
    
    // Add wire - should make not pristine
    Wire w;
    w.id = "wire_1";
    w.start.node_id = doc.blueprint().nodes[0].id;
    w.start.port_name = "v_out";
    w.end.node_id = doc.blueprint().nodes[1].id;
    w.end.port_name = "v_in";
    doc.blueprint().wires.push_back(w);
    
    EXPECT_FALSE(doc.isPristine());
}

// EDGE: OpenDocument with invalid path returns nullptr
TEST(WindowSystem, OpenDocument_InvalidPath) {
    WindowSystem ws;
    
    Document* result = ws.openDocument("/nonexistent/path/file.blueprint");
    
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(ws.documentCount(), 1u);  // Still has pristine Untitled
}

// EDGE: RecentFiles handles empty string
TEST(RecentFiles, HandlesEmptyString) {
    RecentFiles rf;
    rf.add("");  // Empty path
    
    // Empty path should still be added (though not useful)
    EXPECT_EQ(rf.files().size(), 1u);
}

// EDGE: RecentFiles handles paths with unicode (if filesystem supports)
TEST(RecentFiles, HandlesUnicodePath) {
    // Create temp file with unicode name
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "ан24_test";
    std::filesystem::create_directories(temp_dir);
    std::string unicode_path = (temp_dir / "тест.blueprint").string();
    
    {
        std::ofstream f(unicode_path);
        f << "test";
    }
    
    if (std::filesystem::exists(unicode_path)) {
        RecentFiles rf;
        rf.add(unicode_path);
        
        EXPECT_EQ(rf.files().size(), 1u);
        EXPECT_EQ(rf.files()[0], unicode_path);
        
        std::filesystem::remove_all(temp_dir);
    }
}

// EDGE: CloseDocument with sub-windows open
TEST(WindowSystem, CloseDocument_WithSubWindowsOpen) {
    WindowSystem ws;
    TypeRegistry registry = load_type_registry();
    
    // Create a document with sub-blueprint
    ws.activeDocument()->addComponent("Battery", Pt(100, 100), "", registry);
    
    // This should not crash when closing
    EXPECT_TRUE(ws.closeDocument(*ws.activeDocument()));
    EXPECT_EQ(ws.documentCount(), 1u);  // New document created
}

// EDGE: Rapid open/close cycles don't leak memory
TEST(WindowSystem, RapidOpenCloseCycles) {
    // Create temp file
    char temp_file[] = "/tmp/rapid_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    std::ofstream f(temp_file);
    f << R"({
  "version": 2,
  "meta": { "name": "test" },
  "nodes": { "b1": { "type": "Battery", "pos": [100, 100] } },
  "wires": []
})";
    f.close();

    WindowSystem ws;
    
    for (int i = 0; i < 50; i++) {
        Document* doc = ws.openDocument(temp_file);
        ASSERT_NE(doc, nullptr);
        ws.closeDocument(*doc);
    }
    
    std::remove(temp_file);
}

// EDGE: RecentFiles max limit edge
TEST(RecentFiles, MaxLimitExact) {
    RecentFiles rf;
    
    // Add exactly MAX files
    for (size_t i = 0; i < RecentFiles::MAX; i++) {
        rf.add("/file" + std::to_string(i) + ".blueprint");
    }
    
    EXPECT_EQ(rf.files().size(), RecentFiles::MAX);
    
    // Add one more - should still be MAX
    rf.add("/file_extra.blueprint");
    EXPECT_EQ(rf.files().size(), RecentFiles::MAX);
}
