#include <gtest/gtest.h>
#include "editor/window/properties_window.h"
#include "editor/data/node.h"
#include "editor/data/blueprint.h"
#include "editor/undo/undo_stack.h"
#include "ui/core/interned_id.h"

// Allow gtest to print InternedId values on assertion failure
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}

// =============================================================================
// Phase 2: PropertiesWindow Tests — TDD
// =============================================================================

TEST(PropertiesWindow, OpenSetsTarget) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    EXPECT_FALSE(win.isOpen());

    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());
    EXPECT_EQ(win.targetNodeId(), "bat1");
}

TEST(PropertiesWindow, CancelRevertsParams) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Simulate user editing params directly
    node_ptr->params["v"] = "12.0";
    node_ptr->name = "modified_name";

    // Cancel should revert
    win.close();

    // Re-acquire pointer (cancelAndClose resolves fresh)
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "28.0") << "Cancel must revert params";
    EXPECT_EQ(node_ptr->params["r"], "0.01") << "Untouched params preserved";
    EXPECT_EQ(node_ptr->name, "bat1") << "Cancel must revert name";
    EXPECT_FALSE(win.isOpen());
}

TEST(PropertiesWindow, CancelRevertsAddedParam) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // User adds a param that wasn't in the original
    node_ptr->params["new_key"] = "new_value";

    win.close();

    // Re-acquire pointer
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    // snapshot_params_ had only {"v": "28.0"}, so restore should remove "new_key"
    EXPECT_EQ(node_ptr->params.size(), 1u);
    EXPECT_EQ(node_ptr->params.at("v"), "28.0");
    EXPECT_EQ(node_ptr->params.count("new_key"), 0u)
        << "Added param should be removed on cancel";
}

TEST(PropertiesWindow, OpenTwiceOverwritesSnapshot) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;

    // First open
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // User edits
    node_ptr->params["v"] = "12.0";

    // Open again (re-snapshot)
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Cancel now should revert to the second snapshot (v=12.0)
    win.close();
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "12.0")
        << "Second open() should create a new snapshot";
}

TEST(PropertiesWindow, ClosedWindowIsNotOpen) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.params = {};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());

    win.close();
    EXPECT_FALSE(win.isOpen());
}

// =============================================================================
// Phase 3: Apply + Undo Stack Integration Tests
// =============================================================================

TEST(PropertiesWindow, ApplyEmitsCmdSetParam) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};
    bp.add_node(n);

    // Get a pointer to the node in the blueprint
    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Simulate user changing voltage
    node_ptr->params["v"] = "14.0";

    // Apply — should snapshot and apply changes to undo stack
    win.apply();

    EXPECT_FALSE(win.isOpen());
    // Re-acquire pointer after apply (snapshot may have been taken)
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "14.0") << "Applied value must persist";
    EXPECT_EQ(node_ptr->params["r"], "0.01") << "Untouched param preserved";
    EXPECT_TRUE(undo.can_undo()) << "Undo stack must have an entry";
}

TEST(PropertiesWindow, ApplyThenUndoRevertsParam) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // User edits
    node_ptr->params["v"] = "14.0";
    node_ptr->params["r"] = "0.05";

    win.apply();

    // Re-acquire pointer
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "14.0");
    EXPECT_EQ(node_ptr->params["r"], "0.05");

    // Undo — restores snapshot
    ASSERT_TRUE(undo.can_undo());
    undo.undo(bp);

    // Re-acquire pointer after undo (blueprint was replaced)
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "28.0") << "Undo must revert v";
    EXPECT_EQ(node_ptr->params["r"], "0.01") << "Undo must revert r";

    // Redo
    undo.redo(bp);
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "14.0") << "Redo must restore v";
    EXPECT_EQ(node_ptr->params["r"], "0.05") << "Redo must restore r";
}

TEST(PropertiesWindow, ApplyNoChangesDoesNotPushUndo) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // No edits — just apply
    win.apply();

    EXPECT_FALSE(undo.can_undo())
        << "Applying with no changes should not push to undo stack";
}

TEST(PropertiesWindow, ApplyInvokesCallback) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    bool callback_invoked = false;
    std::string callback_node_id;

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo,
        [&](const std::string& nid) {
            callback_invoked = true;
            callback_node_id = nid;
        });

    node_ptr->params["v"] = "14.0";
    win.apply();

    EXPECT_TRUE(callback_invoked) << "Apply must invoke on_apply callback";
    EXPECT_EQ(callback_node_id, "bat1");
}

TEST(PropertiesWindow, NameChangePushesUndo) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "OriginalName";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Change the name
    node_ptr->name = "NewName";
    win.apply();

    EXPECT_TRUE(undo.can_undo()) << "Name change should push to undo stack";
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "NewName");
}

TEST(PropertiesWindow, NameChangeUndoRestoresOldName) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "OriginalName";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Change the name
    node_ptr->name = "NewName";
    win.apply();

    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "NewName");

    // Undo
    undo.undo(bp);

    // Re-acquire pointer after undo
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "OriginalName") << "Undo should restore original name";
}

TEST(PropertiesWindow, ParamAndNameChangeSingleUndo) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "OriginalName";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Change both param and name in one "Apply"
    node_ptr->params["v"] = "14.0";
    node_ptr->name = "NewName";
    win.apply();

    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "14.0");
    EXPECT_EQ(node_ptr->name, "NewName");

    // A single Ctrl+Z should revert BOTH changes
    ASSERT_TRUE(undo.can_undo());
    undo.undo(bp);

    // Re-acquire pointer after undo
    node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->params["v"], "28.0") << "Single undo must revert param";
    EXPECT_EQ(node_ptr->name, "OriginalName") << "Single undo must revert name";
    EXPECT_FALSE(undo.can_undo()) << "Only one undo entry should have been pushed";
}

// =============================================================================
// Phase 4: Pointer Safety — Node disappears while window is open
// =============================================================================

TEST(PropertiesWindow, CloseGracefullyWhenNodeRemoved) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());

    // Simulate the node being removed (e.g. by undo of a CmdAddNode)
    auto node_iid = bp.interner().intern("bat1");
    execute(bp, cmd_remove_node(node_iid));
    EXPECT_EQ(bp.find_node("bat1"), nullptr) << "Node must be gone";

    // render() should detect the missing node and close silently
    win.render();
    EXPECT_FALSE(win.isOpen())
        << "Window must auto-close when target node is removed";
}

TEST(PropertiesWindow, ApplyGracefullyWhenNodeRemoved) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Remove node before apply
    auto node_iid = bp.interner().intern("bat1");
    execute(bp, cmd_remove_node(node_iid));

    // apply() should detect the missing node and close without crashing
    win.apply();
    EXPECT_FALSE(win.isOpen());
    EXPECT_FALSE(undo.can_undo())
        << "No undo entry should be pushed when target node is gone";
}

TEST(PropertiesWindow, CancelGracefullyWhenNodeRemoved) {
    Blueprint bp;
    UndoStack undo;

    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};
    bp.add_node(n);

    Node* node_ptr = bp.find_node("bat1");
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", bp, undo, [](const std::string&) {});

    // Remove node before cancel
    auto node_iid = bp.interner().intern("bat1");
    execute(bp, cmd_remove_node(node_iid));

    // close() (cancel) should not crash even though node is gone
    win.close();
    EXPECT_FALSE(win.isOpen());
}
