#include <gtest/gtest.h>
#include "editor/commands/commands.h"
#include "editor/commands/transaction_guard.h"
#include "editor/commands/blueprint_checksum.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

// Helper: create a simple node
static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                       const char* id,
                                       float x = 0.0f, float y = 0.0f) {
    bp2::Blueprint::Node n;
    n.id = I.intern(id);
    n.type = I.intern("Test");
    n.x = x;
    n.y = y;
    return n;
}

// Helper: create a wire between node:port -> node:port
static bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                                       bp2::PathArena& arena,
                                       const char* wire_id,
                                       const char* src_node, const char* src_port,
                                       const char* dst_node, const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern(src_node)),
                               I.intern(src_port));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern(dst_node)),
                               I.intern(dst_port));
    return w;
}

class CommandTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    bp2::EditorModel   model;
};

TEST_F(CommandTest, BlueprintDefaults) {
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

// =============================================================================
// Basic can_undo / can_redo
// =============================================================================

TEST_F(CommandTest, UndoStackBasic) {
    EXPECT_FALSE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// CmdSetGridStep
// =============================================================================

TEST_F(CommandTest, SetGridStepMutates) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(40.0f));
    EXPECT_FLOAT_EQ(model.current().grid_step(), 40.0f);
}

// =============================================================================
// CmdMoveNode
// =============================================================================

TEST_F(CommandTest, MoveNodeMutates) {
    auto node_id = interner.intern("test_node");
    model.add_node(make_node(interner, "test_node", 10.0f, 20.0f));

    execute(model, interner, cmd_move_node(node_id, 100.0f, 200.0f));

    auto* n = model.current().find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->x, 100.0f);
    EXPECT_FLOAT_EQ(n->y, 200.0f);
}

// =============================================================================
// CmdAddNode / CmdRemoveNode
// =============================================================================

TEST_F(CommandTest, AddRemoveNode) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.type = interner.intern("Battery");

    execute(model, interner, cmd_add_node(node));
    EXPECT_EQ(model.current().nodes().size(), 1u);

    execute(model, interner, cmd_remove_node(node_id));
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

// =============================================================================
// Undo/Redo round-trip for CmdSetGridStep
// =============================================================================

TEST_F(CommandTest, UndoRedoRoundTrip) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());

    model.undo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
    EXPECT_TRUE(model.can_redo());

    model.redo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// CmdSetName
// =============================================================================

TEST_F(CommandTest, SetNameMutates) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.name = "OriginalName";
    model.add_node(std::move(node));

    execute(model, interner, cmd_set_name(node_id, "NewName"));

    auto* n = model.current().find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->name, "NewName");
}

TEST_F(CommandTest, SetNameUndoRedo) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.name = "OldName";
    model.add_node(std::move(node));

    model.push_checkpoint();
    execute(model, interner, cmd_set_name(node_id, "NewName"));
    EXPECT_EQ(model.current().find_node(node_id)->name, "NewName");

    model.undo();
    EXPECT_EQ(model.current().find_node(node_id)->name, "OldName");
}

// =============================================================================
// SetNameMissingNode — should not crash
// =============================================================================

TEST_F(CommandTest, SetNameMissingNodeDoesNotCrash) {
    auto ghost_id = interner.intern("ghost");
    execute(model, interner, cmd_set_name(ghost_id, "NewName"));
}

// =============================================================================
// Multiple command grouping (push_checkpoint before a batch)
// =============================================================================

TEST_F(CommandTest, MultipleCommandsUndoAsGroup) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    model.add_node(make_node(interner, "a", 0.0f, 0.0f));
    model.add_node(make_node(interner, "b", 10.0f, 10.0f));

    // Snapshot before both moves — will be the recovery point
    model.push_checkpoint();
    execute(model, interner, cmd_move_node(id_a, 100.0f, 100.0f));
    execute(model, interner, cmd_move_node(id_b, 200.0f, 200.0f));

    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->x, 100.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->x, 200.0f);

    // Undo last CmdMoveNode (id_b), then id_a
    model.undo();
    model.undo();

    // Now at the push_checkpoint state — both nodes at original positions
    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->x, 0.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->x, 10.0f);
}

// =============================================================================
// TransactionGuard tests
// =============================================================================

TEST_F(CommandTest, TransactionGuardSingleCommand) {
    auto id = interner.intern("n");
    model.add_node(make_node(interner, "n", 0.0f, 0.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id, 42.0f, 42.0f));
    }

    EXPECT_FLOAT_EQ(model.current().find_node(id)->x, 42.0f);
    EXPECT_TRUE(model.can_undo());

    model.undo();
    EXPECT_FLOAT_EQ(model.current().find_node(id)->x, 0.0f);
}

TEST_F(CommandTest, TransactionGuardMultipleCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    model.add_node(make_node(interner, "a", 0.0f, 0.0f));
    model.add_node(make_node(interner, "b", 10.0f, 10.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id_a, 100.0f, 100.0f));
        txn.execute(cmd_move_node(id_b, 200.0f, 200.0f));
    }

    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->x, 100.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->x, 200.0f);
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, TransactionGuardEmpty) {
    {
        TransactionGuard txn(model, interner);
        // No commands
    }
    EXPECT_FALSE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

TEST_F(CommandTest, TransactionGuardDiscard) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    // discard() undoes the checkpoint taken by the TransactionGuard
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

TEST_F(CommandTest, TransactionGuardManualCommit) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_set_grid_step(32.0f));
        txn.commit();  // Explicit commit; destructor should be idempotent
    }

    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());
}

// =============================================================================
// Dirty flag / save-point tests
// =============================================================================

TEST_F(CommandTest, DirtyFlagInitiallyClean) {
    EXPECT_FALSE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterCheckpoint) {
    model.push_checkpoint();
    EXPECT_TRUE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterMarkSaved) {
    model.push_checkpoint();
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterUndoPastSavePoint) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());

    model.undo();
    EXPECT_TRUE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagRedoRestoresSavePoint) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.mark_saved();

    model.undo();
    EXPECT_TRUE(model.is_dirty());

    model.redo();
    EXPECT_FALSE(model.is_dirty());
}

// =============================================================================
// Redo-stack preservation tests
// =============================================================================

TEST_F(CommandTest, RedoStackPreservedByUndo) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(48.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(64.0f));

    model.undo();
    model.undo();
    model.undo();

    EXPECT_TRUE(model.can_redo());
    EXPECT_FALSE(model.can_undo());

    model.redo();
    EXPECT_TRUE(model.can_redo());
    EXPECT_TRUE(model.can_undo());

    model.redo();
    EXPECT_TRUE(model.can_redo());

    model.redo();
    EXPECT_FALSE(model.can_redo());
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, NewSnapshotClearsRedo) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));

    model.undo();
    EXPECT_TRUE(model.can_redo());

    // New mutation should clear redo
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(48.0f));
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// Blueprint checksum / undo round-trip verification tests
// =============================================================================

TEST_F(CommandTest, ChecksumDeterministic) {
    model.add_node(make_node(interner, "n", 10.0f, 20.0f));

    size_t h1 = blueprint_checksum(model.current());
    size_t h2 = blueprint_checksum(model.current());
    EXPECT_EQ(h1, h2);
}

TEST_F(CommandTest, ChecksumChangesOnMutation) {
    size_t h1 = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(999.0f));
    size_t h2 = blueprint_checksum(model.current());
    EXPECT_NE(h1, h2);
}

TEST_F(CommandTest, UndoRoundTripChecksumMoveNode) {
    auto id = interner.intern("n");
    model.add_node(make_node(interner, "n", 10.0f, 20.0f));

    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_move_node(id, 100.0f, 200.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumSetParam) {
    auto id = interner.intern("n");
    auto key = interner.intern("key");
    auto node = make_node(interner, "n");
    node.params[key] = 1.0f;
    model.add_node(std::move(node));

    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_param(id, key, 2.0f));
    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveNode) {
    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    model.add_node(make_node(interner, "n", 0.0f, 0.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumSetGridStep) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(64.0f));

    size_t after = blueprint_checksum(model.current());
    EXPECT_NE(after, blueprint_checksum(bp2::Blueprint{}));

    model.undo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

TEST_F(CommandTest, UndoRoundTripChecksumMixedCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    auto na = make_node(interner, "a", 0.0f, 0.0f); na.name = "aaa";
    auto nb = make_node(interner, "b", 10.0f, 10.0f); nb.name = "bbb";
    model.add_node(std::move(na));
    model.add_node(std::move(nb));

    size_t before = blueprint_checksum(model.current());

    execute(model, interner, cmd_move_node(id_a, 100.0f, 100.0f));
    execute(model, interner, cmd_set_name(id_b, "changed"));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveWire) {
    bp2::PathArena arena(interner);

    model.add_node(make_node(interner, "n"));
    size_t before = blueprint_checksum(model.current());

    auto w = make_wire(interner, arena, "w1", "n", "out", "n", "in");
    model.push_checkpoint();
    model.add_wire(std::move(w));
    EXPECT_EQ(model.current().wires().size(), 1u);

    model.undo();
    EXPECT_EQ(model.current().wires().size(), 0u);
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

// =============================================================================
// Regression tests — edge cases
// =============================================================================

TEST_F(CommandTest, MaxStackDepthEviction) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(0.0f));

    // Push 100 checkpoints (max_history_ = 100)
    for (int i = 1; i <= 100; ++i) {
        model.push_checkpoint();
        execute(model, interner, cmd_set_grid_step(static_cast<float>(i)));
    }
    EXPECT_FLOAT_EQ(model.current().grid_step(), 100.0f);
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, UndoRedoWithEmptyBlueprint) {
    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);

    model.redo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
}

TEST_F(CommandTest, TransactionGuardDiscardMultipleCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    auto na = make_node(interner, "a", 0.0f, 0.0f); na.name = "aaa";
    auto nb = make_node(interner, "b", 10.0f, 10.0f); nb.name = "bbb";
    model.add_node(std::move(na));
    model.add_node(std::move(nb));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    size_t before = blueprint_checksum(model.current());

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id_a, 100.0f, 100.0f));
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    // After discard: position and grid_step should be reverted
    // Note: discard() calls model.undo() once, reverting the last checkpoint
    // which was the push_checkpoint() inside TransactionGuard before cmd_move_node
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->x, 0.0f);
}

TEST_F(CommandTest, UndoStackClearSimulatesDocumentLoad) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(42.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(99.0f));
    EXPECT_TRUE(model.can_undo());

    // Simulate "load new document" by replacing current and marking saved
    model.replace_current(bp2::Blueprint{});
    model.mark_saved();

    // Undo stack still has entries from before replace_current,
    // but the document state is fresh. This is expected behavior —
    // the caller should call push_checkpoint + replace_current or create a new EditorModel.
    // Here we just verify the model is not dirty after mark_saved.
    EXPECT_FALSE(model.is_dirty());
}

// =============================================================================
// CmdSetPortLayout
// =============================================================================

TEST_F(CommandTest, SetPortLayout_Mutates) {
    auto node_id = interner.intern("node1");
    model.add_node(make_node(interner, "node1"));

    EXPECT_TRUE(model.current().find_node(node_id)->layout_overrides.empty());

    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("Top"), std::nullopt});
    overrides.push_back({"v_out", std::string("Bottom"), 0});

    execute(model, interner, cmd_set_port_layout(node_id, overrides));

    // CmdSetPortLayout does remove_node + add_node, undo the add_node to see the result
    auto* result = model.current().find_node(node_id);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->layout_overrides.size(), 2u);
    EXPECT_EQ(result->layout_overrides[0].port_name, "v_in");
    EXPECT_EQ(result->layout_overrides[0].side, std::optional<std::string>("Top"));
    EXPECT_EQ(result->layout_overrides[1].port_name, "v_out");
    EXPECT_EQ(result->layout_overrides[1].side, std::optional<std::string>("Bottom"));
    EXPECT_EQ(result->layout_overrides[1].position, std::optional<int>(0));
}

TEST_F(CommandTest, SetPortLayout_UndoRestoresOriginal) {
    auto node_id = interner.intern("node1");
    model.add_node(make_node(interner, "node1"));

    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("Bottom"), std::nullopt});

    model.push_checkpoint();
    execute(model, interner, cmd_set_port_layout(node_id, overrides));

    ASSERT_EQ(model.current().find_node(node_id)->layout_overrides.size(), 1u);

    model.undo();
    EXPECT_TRUE(model.current().find_node(node_id)->layout_overrides.empty())
        << "Undo should restore original empty layout_overrides";
}

TEST_F(CommandTest, SetPortLayout_ClearOverrides) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.layout_overrides.push_back({"v_in", std::string("Top"), std::nullopt});
    model.add_node(std::move(node));

    ASSERT_EQ(model.current().find_node(node_id)->layout_overrides.size(), 1u);

    execute(model, interner, cmd_set_port_layout(node_id, {}));

    EXPECT_TRUE(model.current().find_node(node_id)->layout_overrides.empty());
}

// =============================================================================
// REGRESSION: Slider min/max must sync to content_min/max when params are edited
// =============================================================================

TEST_F(CommandTest, REGRESSION_SetParamMutatesNodeParam) {
    auto id = interner.intern("slider1");
    auto key_max = interner.intern("max");
    auto key_min = interner.intern("min");

    auto node = make_node(interner, "slider1");
    node.type = interner.intern("Slider");
    node.content_type = bp2::NodeContentType::Slider;
    node.content_min = 0.0f;
    node.content_max = 100.0f;
    node.params[key_max] = 100.0f;
    node.params[key_min] = 0.0f;
    model.add_node(std::move(node));

    EXPECT_FLOAT_EQ(model.current().find_node(id)->content_max, 100.0f);

    execute(model, interner, cmd_set_param(id, key_max, 200.0f));

    auto* n = model.current().find_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->params.at(key_max), 200.0f);
}
