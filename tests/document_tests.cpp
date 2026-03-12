#include <gtest/gtest.h>
#include "editor/document.h"
#include "editor/window_system.h"
#include "json_parser/json_parser.h"
#include <fstream>
#include <cstdio>

// Minimal stubs for ImGui dependency
#define IMGUI_API
typedef int ImGuiKey;
struct ImVec2 { float x, y; ImVec2(float x_=0, float y_=0) : x(x_), y(y_) {} };
namespace ImGui {
    inline bool IsKeyPressed(ImGuiKey) { return false; }
    inline bool IsMouseClicked(int) { return false; }
    inline bool IsMouseDragging(int) { return false; }
    inline ImVec2 GetMouseDragDelta(int) { return ImVec2(); }
    inline void ResetMouseDragDelta(int) {}
}


// ============================================================================
// Document Tests
// ============================================================================

TEST(DocumentTest, CreateUntitled) {
    Document doc;

    EXPECT_EQ(doc.filepath(), "");
    EXPECT_EQ(doc.displayName(), "Untitled");
    EXPECT_TRUE(doc.title().find("Untitled") != std::string::npos);
    EXPECT_FALSE(doc.isSimulationRunning());
}

TEST(DocumentTest, UniqueIds) {
    Document doc1;
    Document doc2;
    Document doc3;

    EXPECT_NE(doc1.id(), doc2.id());
    EXPECT_NE(doc2.id(), doc3.id());
    EXPECT_NE(doc1.id(), doc3.id());
}

TEST(DocumentTest, SaveLoadRoundtrip) {
    // Create a temporary file
    char temp_file[] = "/tmp/blueprint_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    // Create document and add a component
    Document doc1;
    TypeRegistry registry = load_type_registry();

    doc1.addComponent("Battery", Pt(100, 100), "", registry);

    // Save
    EXPECT_TRUE(doc1.save(temp_file));
    EXPECT_EQ(doc1.filepath(), temp_file);
    EXPECT_TRUE(doc1.displayName().find("blueprint_test_") != std::string::npos);

    // Load into another document
    Document doc2;
    EXPECT_TRUE(doc2.load(temp_file));

    EXPECT_EQ(doc2.filepath(), temp_file);
    EXPECT_EQ(doc2.blueprint().nodes.size(), doc1.blueprint().nodes.size());

    // Clean up
    std::remove(temp_file);
}

TEST(DocumentTest, SaveSyncsViewport) {
    char temp_file[] = "/tmp/blueprint_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    Document doc1;
    TypeRegistry registry = load_type_registry();

    // Modify viewport
    doc1.scene().viewport().pan = Pt(50, 50);
    doc1.scene().viewport().zoom = 2.0f;
    doc1.scene().viewport().grid_step = 20;

    // Save
    EXPECT_TRUE(doc1.save(temp_file));

    // Load and verify viewport was saved
    Document doc2;
    EXPECT_TRUE(doc2.load(temp_file));

    EXPECT_FLOAT_EQ(doc2.scene().viewport().pan.x, 50);
    EXPECT_FLOAT_EQ(doc2.scene().viewport().pan.y, 50);
    EXPECT_FLOAT_EQ(doc2.scene().viewport().zoom, 2.0f);
    EXPECT_FLOAT_EQ(doc2.scene().viewport().grid_step, 20);

    std::remove(temp_file);
}

TEST(DocumentTest, SimulationControl) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("Battery", Pt(0, 0), "", registry);

    EXPECT_FALSE(doc.isSimulationRunning());

    doc.startSimulation();
    EXPECT_TRUE(doc.isSimulationRunning());

    doc.stopSimulation();
    EXPECT_FALSE(doc.isSimulationRunning());
}

TEST(DocumentTest, SimulationStepOnlyWhenRunning) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("Battery", Pt(0, 0), "", registry);

    // Should not crash when simulation not running
    EXPECT_NO_THROW(doc.updateSimulationStep(0.016f));

    doc.startSimulation();
    EXPECT_NO_THROW(doc.updateSimulationStep(0.016f));

    doc.stopSimulation();
    EXPECT_NO_THROW(doc.updateSimulationStep(0.016f));
}

TEST(DocumentTest, AddComponentCreatesUniqueNode) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("Battery", Pt(100, 100), "", registry);
    doc.addComponent("Battery", Pt(200, 200), "", registry);

    EXPECT_EQ(doc.blueprint().nodes.size(), 2);

    // IDs should be unique
    EXPECT_NE(doc.blueprint().nodes[0].id, doc.blueprint().nodes[1].id);

    // Both should start with "battery_"
    EXPECT_TRUE(doc.blueprint().nodes[0].id.find("battery_") == 0);
    EXPECT_TRUE(doc.blueprint().nodes[1].id.find("battery_") == 0);
}

TEST(DocumentTest, TriggerSwitch) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("Switch", Pt(0, 0), "", registry);

    std::string switch_id = doc.blueprint().nodes[0].id;

    // Should not crash
    EXPECT_NO_THROW(doc.triggerSwitch(switch_id));

    // Override should be set
    EXPECT_FALSE(doc.signalOverrides().empty());
}

TEST(DocumentTest, HoldButtonPressRelease) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("HoldButton", Pt(0, 0), "", registry);

    std::string button_id = doc.blueprint().nodes[0].id;

    EXPECT_NO_THROW(doc.holdButtonPress(button_id));
    EXPECT_TRUE(doc.heldButtons().count(button_id) > 0);

    EXPECT_NO_THROW(doc.holdButtonRelease(button_id));
    EXPECT_TRUE(doc.heldButtons().count(button_id) == 0);
}

TEST(DocumentTest, OpenSubWindow) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    // Test that the method exists and doesn't crash
    EXPECT_NO_THROW(doc.openSubWindow("test_group"));
}

TEST(DocumentTest, ApplyInputResultReturnsAction) {
    Document doc;
    TypeRegistry registry = load_type_registry();

    doc.addComponent("Battery", Pt(0, 0), "", registry);

    // Test that applyInputResult returns the correct action struct
    InputResult empty_result;
    auto action = doc.applyInputResult(empty_result, "");

    EXPECT_FALSE(action.show_context_menu);
    EXPECT_FALSE(action.show_node_context_menu);
}

// ============================================================================
// WindowSystem Tests
// ============================================================================

TEST(WindowSystemTest, CreateInitialDocument) {
    WindowSystem ws;

    EXPECT_EQ(ws.documentCount(), 1);
    EXPECT_NE(ws.activeDocument(), nullptr);
    EXPECT_EQ(ws.activeDocument()->displayName(), "Untitled");
}

TEST(WindowSystemTest, CreateMultipleDocuments) {
    WindowSystem ws;

    ws.createDocument();
    ws.createDocument();

    EXPECT_EQ(ws.documentCount(), 3);  // 1 initial + 2 created
}

TEST(WindowSystemTest, CreateDocumentSetsAsActive) {
    WindowSystem ws;

    Document* first = ws.activeDocument();
    Document* second = &ws.createDocument();

    EXPECT_EQ(ws.activeDocument(), second);
    EXPECT_NE(ws.activeDocument(), first);
}

TEST(WindowSystemTest, OpenDocumentCreatesNewIfNotExists) {
    WindowSystem ws;

    char temp_file[] = "/tmp/blueprint_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    // Create and save a document
    {
        Document doc;
        TypeRegistry registry = load_type_registry();
        doc.addComponent("Battery", Pt(100, 100), "", registry);
        doc.save(temp_file);
    }

    // Open it in WindowSystem
    size_t count_before = ws.documentCount();
    Document* doc = ws.openDocument(temp_file);

    EXPECT_NE(doc, nullptr);
    EXPECT_EQ(ws.documentCount(), count_before + 1);
    EXPECT_EQ(doc->filepath(), temp_file);

    std::remove(temp_file);
}

TEST(WindowSystemTest, OpenDocumentReusesIfAlreadyOpen) {
    WindowSystem ws;

    char temp_file[] = "/tmp/blueprint_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    // Create and save a document
    {
        Document doc;
        TypeRegistry registry = load_type_registry();
        doc.addComponent("Battery", Pt(100, 100), "", registry);
        doc.save(temp_file);
    }

    // Open it twice
    Document* doc1 = ws.openDocument(temp_file);
    Document* doc2 = ws.openDocument(temp_file);

    EXPECT_EQ(doc1, doc2);  // Should return the same document
    EXPECT_EQ(ws.documentCount(), 2);  // 1 initial + 1 opened

    std::remove(temp_file);
}

TEST(WindowSystemTest, FindDocumentByPath) {
    WindowSystem ws;

    char temp_file[] = "/tmp/blueprint_test_XXXXXX";
    int fd = mkstemp(temp_file);
    ASSERT_GE(fd, 0);
    close(fd);

    // Create and save a document
    {
        Document doc;
        TypeRegistry registry = load_type_registry();
        doc.addComponent("Battery", Pt(100, 100), "", registry);
        doc.save(temp_file);
    }

    Document* opened = ws.openDocument(temp_file);
    Document* found = ws.findDocumentByPath(temp_file);

    EXPECT_EQ(opened, found);

    std::remove(temp_file);
}

TEST(WindowSystemTest, CloseDocumentRemovesFromList) {
    WindowSystem ws;

    Document& doc = ws.createDocument();
    size_t count_before = ws.documentCount();

    // Document is not modified, so close should succeed
    EXPECT_TRUE(ws.closeDocument(doc));
    EXPECT_EQ(ws.documentCount(), count_before - 1);
}

TEST(WindowSystemTest, CloseDocumentSwitchesActive) {
    WindowSystem ws;

    Document& doc1 = ws.createDocument();
    Document& doc2 = ws.createDocument();

    ws.setActiveDocument(&doc2);

    EXPECT_EQ(ws.activeDocument(), &doc2);

    ws.closeDocument(doc1);

    // Active should still be doc2 (or switched to another valid doc)
    EXPECT_NE(ws.activeDocument(), nullptr);
}

TEST(WindowSystemTest, CloseLastDocumentCreatesNew) {
    WindowSystem ws;

    // Close all but one
    while (ws.documentCount() > 1) {
        ws.closeDocument(*ws.documents()[0]);
    }

    Document* last = ws.activeDocument();
    EXPECT_NE(last, nullptr);

    // Close the last one - should create a new untitled
    ws.closeDocument(*last);

    EXPECT_NE(ws.activeDocument(), nullptr);
    EXPECT_EQ(ws.documentCount(), 1);
}

TEST(WindowSystemTest, CloseDocumentReturnsFalseIfModified) {
    WindowSystem ws;

    Document& doc = ws.createDocument();
    TypeRegistry registry = load_type_registry();
    doc.addComponent("Battery", Pt(0, 0), "", registry);

    // Document is modified - close should return false
    EXPECT_FALSE(ws.closeDocument(doc));
    EXPECT_EQ(ws.documentCount(), 2);  // Document not removed
}

TEST(WindowSystemTest, SetActiveDocumentUpdatesInspector) {
    WindowSystem ws;

    Document& doc1 = ws.createDocument();
    Document& doc2 = ws.createDocument();

    ws.setActiveDocument(&doc1);

    // Add component to doc1 - inspector should see it
    TypeRegistry registry = load_type_registry();
    doc1.addComponent("Battery", Pt(0, 0), "", registry);

    // Inspector should have doc1's scene
    // (We can't directly test this without accessing internal inspector state,
    // but we can verify it doesn't crash)
    // Note: Inspector::render() requires ImGui context, skip for now
}

TEST(WindowSystemTest, InspectorFollowsActiveDocument) {
    WindowSystem ws;

    Document& doc1 = ws.createDocument();
    Document& doc2 = ws.createDocument();

    // Switch between documents
    ws.setActiveDocument(&doc1);
    EXPECT_EQ(ws.activeDocument(), &doc1);

    ws.setActiveDocument(&doc2);
    EXPECT_EQ(ws.activeDocument(), &doc2);
}

TEST(WindowSystemTest, TypeRegistryIsAccessible) {
    WindowSystem ws;

    TypeRegistry& registry = ws.typeRegistry();

    // Verify registry has some components
    EXPECT_TRUE(registry.has("Battery"));
    EXPECT_TRUE(registry.has("Switch"));
}

TEST(WindowSystemTest, ContextMenuHasSourceDoc) {
    WindowSystem ws;

    Document& doc = ws.createDocument();

    Document::InputResultAction action;
    action.show_context_menu = true;
    action.context_menu_pos = Pt(100, 100);
    action.context_menu_group_id = "";

    ws.handleInputAction(action, doc);

    EXPECT_TRUE(ws.contextMenu.show);
    EXPECT_EQ(ws.contextMenu.source_doc, &doc);
}

TEST(WindowSystemTest, NodeContextMenuHasSourceDoc) {
    WindowSystem ws;

    Document& doc = ws.createDocument();

    Document::InputResultAction action;
    action.show_node_context_menu = true;
    action.context_menu_node_index = 0;
    action.node_context_menu_group_id = "";

    ws.handleInputAction(action, doc);

    EXPECT_TRUE(ws.nodeContextMenu.show);
    EXPECT_EQ(ws.nodeContextMenu.source_doc, &doc);
    EXPECT_EQ(ws.nodeContextMenu.node_index, 0);
}

TEST(WindowSystemTest, OpenColorPickerForNode) {
    WindowSystem ws;

    Document& doc = ws.createDocument();
    TypeRegistry registry = load_type_registry();
    doc.addComponent("Battery", Pt(0, 0), "", registry);

    ws.openColorPickerForNode(0, "", doc);

    EXPECT_TRUE(ws.colorPicker.show);
    EXPECT_EQ(ws.colorPicker.source_doc, &doc);
    EXPECT_EQ(ws.colorPicker.node_index, 0);
}

TEST(WindowSystemTest, OpenPropertiesForNode) {
    WindowSystem ws;

    Document& doc = ws.createDocument();
    TypeRegistry registry = load_type_registry();
    doc.addComponent("Battery", Pt(0, 0), "", registry);

    // Should not crash
    EXPECT_NO_THROW(ws.openPropertiesForNode(0, doc));
}

TEST(WindowSystemTest, ShowInspectorToggle) {
    WindowSystem ws;

    bool initial = ws.showInspector;

    ws.showInspector = !ws.showInspector;

    EXPECT_EQ(ws.showInspector, !initial);
}

TEST(WindowSystemTest, DocumentsVectorIsNotEmpty) {
    WindowSystem ws;

    EXPECT_FALSE(ws.documents().empty());
    EXPECT_GE(ws.documents().size(), 1);
}

TEST(WindowSystemTest, EachDocumentHasUniqueId) {
    WindowSystem ws;

    ws.createDocument();
    ws.createDocument();
    ws.createDocument();

    std::vector<std::string> ids;
    for (const auto& doc : ws.documents()) {
        ids.push_back(doc->id());
    }

    // All IDs should be unique
    std::sort(ids.begin(), ids.end());
    auto last = std::unique(ids.begin(), ids.end());
    EXPECT_EQ(last, ids.end());
}
