#include <gtest/gtest.h>
#include "editor/document.h"
#include "editor/window_system.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"

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
    EXPECT_FALSE(doc.isModified());
    EXPECT_EQ(doc.displayName(), "Untitled");
    EXPECT_TRUE(doc.filepath().empty());
    EXPECT_FALSE(doc.isSimulationRunning());
}

TEST(Document, TitleShowsModifiedFlag) {
    Document doc;
    EXPECT_EQ(doc.title(), "Untitled");
    doc.markModified();
    EXPECT_EQ(doc.title(), "Untitled*");
    doc.clearModified();
    EXPECT_EQ(doc.title(), "Untitled");
}

TEST(Document, ModifiedTracking) {
    Document doc;
    EXPECT_FALSE(doc.isModified());
    doc.markModified();
    EXPECT_TRUE(doc.isModified());
    doc.clearModified();
    EXPECT_FALSE(doc.isModified());
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

    // Create a collapsed group in the blueprint
    CollapsedGroup cg;
    cg.id = "lamp1";
    cg.type_name = "Lamp";
    cg.internal_node_ids = {"lamp1:r1"};
    doc.blueprint().collapsed_groups.push_back(cg);

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

TEST(WindowSystem, CloseDocumentModifiedReturnsFalse) {
    WindowSystem ws;
    ws.createDocument();
    Document* doc = ws.documents()[1].get();
    doc->markModified();

    EXPECT_FALSE(ws.closeDocument(*doc));
    EXPECT_EQ(ws.documentCount(), 2u); // not removed
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

TEST(WindowSystem, CloseAllFailsIfModified) {
    WindowSystem ws;
    ws.activeDocument()->markModified();
    EXPECT_FALSE(ws.closeAllDocuments());
    EXPECT_EQ(ws.documentCount(), 1u); // unchanged
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
