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

static ui::StringInterner g_interner;

// =============================================================================
// Phase 2: PropertiesWindow Tests — TDD
// =============================================================================

TEST(PropertiesWindow, OpenSetsTarget) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};

    Blueprint bp;
    UndoStack undo;
    PropertiesWindow win;
    EXPECT_FALSE(win.isOpen());

    win.open(n, "bat1", bp, undo, [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());
    EXPECT_EQ(win.targetNodeId(), "bat1");
}

TEST(PropertiesWindow, CancelRevertsParams) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};

    Blueprint bp;
    UndoStack undo;
    PropertiesWindow win;
    win.open(n, "bat1", bp, undo, [](const std::string&) {});

    // Simulate user editing params directly
    n.params["v"] = "12.0";
    n.name = "modified_name";

    // Cancel should revert
    win.close();

    EXPECT_EQ(n.params["v"], "28.0") << "Cancel must revert params";
    EXPECT_EQ(n.params["r"], "0.01") << "Untouched params preserved";
    EXPECT_EQ(n.name, "bat1") << "Cancel must revert name";
    EXPECT_FALSE(win.isOpen());
}

TEST(PropertiesWindow, CancelRevertsAddedParam) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};

    Blueprint bp;
    UndoStack undo;
    PropertiesWindow win;
    win.open(n, "bat1", bp, undo, [](const std::string&) {});

    // User adds a param that wasn't in the original
    n.params["new_key"] = "new_value";

    win.close();

    // snapshot_params_ had only {"v": "28.0"}, so restore should remove "new_key"
    EXPECT_EQ(n.params.size(), 1u);
    EXPECT_EQ(n.params.at("v"), "28.0");
    EXPECT_EQ(n.params.count("new_key"), 0u)
        << "Added param should be removed on cancel";
}

TEST(PropertiesWindow, OpenTwiceOverwritesSnapshot) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};

    Blueprint bp;
    UndoStack undo;
    PropertiesWindow win;

    // First open
    win.open(n, "bat1", bp, undo, [](const std::string&) {});

    // User edits
    n.params["v"] = "12.0";

    // Open again (re-snapshot)
    win.open(n, "bat1", bp, undo, [](const std::string&) {});

    // Cancel now should revert to the second snapshot (v=12.0)
    win.close();
    EXPECT_EQ(n.params["v"], "12.0")
        << "Second open() should create a new snapshot";
}

TEST(PropertiesWindow, ClosedWindowIsNotOpen) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.params = {};

    Blueprint bp;
    UndoStack undo;
    PropertiesWindow win;
    win.open(n, "bat1", bp, undo, [](const std::string&) {});
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

    // Apply — should emit CmdSetParam to undo stack
    win.apply();

    EXPECT_FALSE(win.isOpen());
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

    EXPECT_EQ(node_ptr->params["v"], "14.0");
    EXPECT_EQ(node_ptr->params["r"], "0.05");

    // Undo
    ASSERT_TRUE(undo.can_undo());
    Command inverse = undo.pop_undo();
    Command redo_cmd = execute(bp, inverse);

    EXPECT_EQ(node_ptr->params["v"], "28.0") << "Undo must revert v";
    EXPECT_EQ(node_ptr->params["r"], "0.01") << "Undo must revert r";

    // Redo
    (void)execute(bp, redo_cmd);
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
    EXPECT_EQ(node_ptr->name, "NewName");

    // Undo
    Command undo_cmd = undo.pop_undo();
    (void)execute(bp, undo_cmd);

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

    EXPECT_EQ(node_ptr->params["v"], "14.0");
    EXPECT_EQ(node_ptr->name, "NewName");

    // A single Ctrl+Z should revert BOTH changes
    ASSERT_TRUE(undo.can_undo());
    Command undo_cmd = undo.pop_undo();
    (void)execute(bp, undo_cmd);

    EXPECT_EQ(node_ptr->params["v"], "28.0") << "Single undo must revert param";
    EXPECT_EQ(node_ptr->name, "OriginalName") << "Single undo must revert name";
    EXPECT_FALSE(undo.can_undo()) << "Only one undo entry should have been pushed";
}
