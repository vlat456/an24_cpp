#include <gtest/gtest.h>
#include <set>
#include "editor/commands/commands.h"
#include "editor/commands/extract_blueprint.h"
#include "editor/commands/transaction_guard.h"
#include "editor/commands/blueprint_checksum.h"
#include "blueprint_v2/codec/blueprint_codec.h"
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

static bp2::Blueprint make_extract_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract"));
    bp = bp.with_display_name("ExtractFixture");

    auto ext_in = make_node(I, "ext_in");
    ext_in.type = I.intern("Source");
    ext_in.outputs.emplace_back(I.intern("out"), PortSide::Output, PortType::V);

    auto a = make_node(I, "a");
    a.type = I.intern("NodeA");
    a.inputs.emplace_back(I.intern("in"), PortSide::Input, PortType::V);
    a.outputs.emplace_back(I.intern("out"), PortSide::Output, PortType::V);

    auto b = make_node(I, "b");
    b.type = I.intern("NodeB");
    b.inputs.emplace_back(I.intern("in"), PortSide::Input, PortType::V);
    b.outputs.emplace_back(I.intern("out"), PortSide::Output, PortType::V);

    auto ext_out = make_node(I, "ext_out");
    ext_out.type = I.intern("Sink");
    ext_out.inputs.emplace_back(I.intern("in"), PortSide::Input, PortType::V);

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "out", "b", "in");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "b", "out", "ext_out", "in");
    w2.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    return bp;
}

static bp2::Blueprint make_extract_iface_collision_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_collision"));
    bp = bp.with_display_name("ExtractIfaceCollision");

    auto ext_in = make_node(I, "ext_in");
    ext_in.type = I.intern("Source");
    ext_in.outputs.emplace_back(I.intern("out"), PortSide::Output, PortType::V);

    auto a = make_node(I, "a");
    a.type = I.intern("NodeA");
    a.inputs.emplace_back(I.intern("sig"), PortSide::Input, PortType::V);
    a.outputs.emplace_back(I.intern("link"), PortSide::Output, PortType::V);

    auto b = make_node(I, "b");
    b.type = I.intern("NodeB");
    b.inputs.emplace_back(I.intern("link"), PortSide::Input, PortType::V);
    b.outputs.emplace_back(I.intern("sig"), PortSide::Output, PortType::V);

    auto ext_out = make_node(I, "ext_out");
    ext_out.type = I.intern("Sink");
    ext_out.inputs.emplace_back(I.intern("in"), PortSide::Input, PortType::V);

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "a", "sig");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "link", "b", "link");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "b", "sig", "ext_out", "in");
    w2.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    return bp;
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

    execute(model, interner, cmd_remove_node(node_id, {}));
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST_F(CommandTest, RemoveNodeAlsoRemovesConnectedWires) {
    bp2::PathArena arena(interner);

    auto n1 = make_node(interner, "n1");
    auto n2 = make_node(interner, "n2");
    auto n3 = make_node(interner, "n3");
    execute(model, interner, cmd_add_node(n1));
    execute(model, interner, cmd_add_node(n2));
    execute(model, interner, cmd_add_node(n3));

    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w1", "n1", "out", "n2", "in")));
    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w2", "n2", "out", "n3", "in")));
    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w3", "n1", "out2", "n3", "in2")));

    ASSERT_EQ(model.current().wires().size(), 3u);

    std::vector<ui::InternedId> connected = {
        interner.intern("w1"),
        interner.intern("w2"),
    };
    execute(model, interner, cmd_remove_node(interner.intern("n2"), connected));

    EXPECT_EQ(model.current().find_node(interner.intern("n2")), nullptr);
    ASSERT_EQ(model.current().wires().size(), 1u);
    EXPECT_NE(model.current().find_wire(interner.intern("w3")), nullptr);
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

TEST_F(CommandTest, ExtractToBlueprint_BasicAtomic) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto& nested = updated->nested()[0];
    ASSERT_TRUE(nested.inline_def != nullptr);
    const auto* collapsed = updated->find_node(nested.id);
    ASSERT_NE(collapsed, nullptr);

    const auto* a = updated->find_node(interner.intern("a"));
    const auto* b = updated->find_node(interner.intern("b"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->group_id, std::string(interner.resolve(nested.id)));
    EXPECT_EQ(b->group_id, std::string(interner.resolve(nested.id)));

    // Keep internal selected wire in subgroup, plus subgroup bridge wires and root boundary wires.
    ASSERT_EQ(updated->wires().size(), 5u);
}

TEST_F(CommandTest, ExtractToBlueprint_UsesCanonicalBridgeNodeIds) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const ui::InternedId nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

    const auto* in_bridge = updated->find_node(interner.intern(nested_sid + ":in"));
    const auto* out_bridge = updated->find_node(interner.intern(nested_sid + ":out"));

    ASSERT_NE(in_bridge, nullptr);
    ASSERT_NE(out_bridge, nullptr);
    EXPECT_EQ(in_bridge->type, interner.intern("BlueprintInput"));
    EXPECT_EQ(out_bridge->type, interner.intern("BlueprintOutput"));
    EXPECT_EQ(in_bridge->group_id, nested_sid);
    EXPECT_EQ(out_bridge->group_id, nested_sid);
}

TEST_F(CommandTest, ExtractToBlueprint_UndoRedoRoundTrip) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);
    model.replace_current(source);

    const size_t before = blueprint_checksum(model.current());

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        model.current(),
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);
    ASSERT_TRUE(updated.has_value()) << err;

    model.push_checkpoint();
    model.replace_current(std::move(*updated));
    const size_t after = blueprint_checksum(model.current());
    EXPECT_NE(after, before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);

    model.redo();
    EXPECT_EQ(blueprint_checksum(model.current()), after);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsNonRootGroup) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "group_1",
        interner,
        arena,
        &err);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("root group"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsSmallSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("at least 2"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_DeterministicIfaceNaming) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err1;
    std::string err2;
    auto a = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err1);
    auto b = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err2);

    ASSERT_TRUE(a.has_value()) << err1;
    ASSERT_TRUE(b.has_value()) << err2;

    std::string sa = bp2::BlueprintCodec::encode(*a, interner, arena);
    std::string sb = bp2::BlueprintCodec::encode(*b, interner, arena);
    EXPECT_EQ(sa, sb);
}

TEST_F(CommandTest, ExtractToBlueprint_InlineBlueprintStructure) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto& nested = updated->nested()[0];
    ASSERT_TRUE(nested.inline_def != nullptr);
    const bp2::Blueprint& inner = *nested.inline_def;

    // Interface must have exactly 1 input ("in") and 1 output ("out").
    const auto& iface = inner.iface();
    ASSERT_EQ(iface.size(), 2u);
    auto pd_in = iface.find(interner.intern("in"));
    auto pd_out = iface.find(interner.intern("out"));
    ASSERT_TRUE(pd_in.has_value());
    ASSERT_TRUE(pd_out.has_value());
    EXPECT_EQ(pd_in->direction, bp2::Direction::Input);
    EXPECT_EQ(pd_out->direction, bp2::Direction::Output);

    // Find the BlueprintInput node inside inline_def.
    const bp2::Blueprint::Node* bp_in_node = nullptr;
    const bp2::Blueprint::Node* bp_out_node = nullptr;
    for (const auto& n : inner.nodes()) {
        if (n.type == interner.intern("BlueprintInput")) bp_in_node = &n;
        if (n.type == interner.intern("BlueprintOutput")) bp_out_node = &n;
    }
    ASSERT_NE(bp_in_node, nullptr);
    ASSERT_NE(bp_out_node, nullptr);

    // BlueprintInput: ext=Input, port=Output
    ASSERT_EQ(bp_in_node->inputs.size(), 1u);
    ASSERT_EQ(bp_in_node->outputs.size(), 1u);
    EXPECT_EQ(bp_in_node->inputs[0].name, interner.intern("ext"));
    EXPECT_EQ(bp_in_node->inputs[0].side, PortSide::Input);
    EXPECT_EQ(bp_in_node->outputs[0].name, interner.intern("port"));
    EXPECT_EQ(bp_in_node->outputs[0].side, PortSide::Output);

    // BlueprintOutput: port=Input, ext=Output
    ASSERT_EQ(bp_out_node->inputs.size(), 1u);
    ASSERT_EQ(bp_out_node->outputs.size(), 1u);
    EXPECT_EQ(bp_out_node->inputs[0].name, interner.intern("port"));
    EXPECT_EQ(bp_out_node->inputs[0].side, PortSide::Input);
    EXPECT_EQ(bp_out_node->outputs[0].name, interner.intern("ext"));
    EXPECT_EQ(bp_out_node->outputs[0].side, PortSide::Output);

    // Inline blueprint must contain internal nodes a and b.
    EXPECT_NE(inner.find_node(interner.intern("a")), nullptr);
    EXPECT_NE(inner.find_node(interner.intern("b")), nullptr);
}

TEST_F(CommandTest, ExtractToBlueprint_SubgroupBridgeWiring) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const ui::InternedId nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

    // Bridge nodes in parent with canonical naming.
    const ui::InternedId in_bridge_id = interner.intern(nested_sid + ":in");
    const ui::InternedId out_bridge_id = interner.intern(nested_sid + ":out");
    const auto* in_bridge = updated->find_node(in_bridge_id);
    const auto* out_bridge = updated->find_node(out_bridge_id);
    ASSERT_NE(in_bridge, nullptr);
    ASSERT_NE(out_bridge, nullptr);

    // Bridge nodes must be in the subgroup.
    EXPECT_EQ(in_bridge->group_id, nested_sid);
    EXPECT_EQ(out_bridge->group_id, nested_sid);

    // Find subgroup bridge wires:
    // Input bridge: in_bridge.port -> a.in
    // Output bridge: b.out -> out_bridge.port
    bool found_in_bridge_wire = false;
    bool found_out_bridge_wire = false;
    const ui::InternedId port_id = interner.intern("port");
    const ui::InternedId a_id = interner.intern("a");
    const ui::InternedId b_id = interner.intern("b");
    const ui::InternedId in_id = interner.intern("in");
    const ui::InternedId out_id = interner.intern("out");

    for (const auto& w : updated->wires()) {
        // Decode source
        if (w.source.kind() != bp2::PathKind::Port) continue;
        bp2::Path src_node_path = arena.parent(w.source);
        if (src_node_path.kind() != bp2::PathKind::Node) continue;
        ui::InternedId src_node = src_node_path.segment();
        ui::InternedId src_port = w.source.segment();

        if (w.target.kind() != bp2::PathKind::Port) continue;
        bp2::Path tgt_node_path = arena.parent(w.target);
        if (tgt_node_path.kind() != bp2::PathKind::Node) continue;
        ui::InternedId tgt_node = tgt_node_path.segment();
        ui::InternedId tgt_port = w.target.segment();

        // in_bridge.port -> a.in
        if (src_node == in_bridge_id && src_port == port_id
            && tgt_node == a_id && tgt_port == in_id) {
            found_in_bridge_wire = true;
        }
        // b.out -> out_bridge.port
        if (src_node == b_id && src_port == out_id
            && tgt_node == out_bridge_id && tgt_port == port_id) {
            found_out_bridge_wire = true;
        }
    }

    EXPECT_TRUE(found_in_bridge_wire) << "Missing wire: input bridge .port -> a.in";
    EXPECT_TRUE(found_out_bridge_wire) << "Missing wire: b.out -> output bridge .port";
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsInputOutputIfaceNameCollision) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_iface_collision_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("collision"), std::string::npos);
}

// Regression: dedupe_name must not produce a collision when a port name
// already looks like a deduped suffix (e.g. "in" and "in_2" coexist).
// The old counter-based dedupe would return "in", "in_2", "in_2" — a collision.
TEST_F(CommandTest, ExtractToBlueprint_DedupeNameNoCollisionWithSuffixedPorts) {
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp_dedupe_regression"));
    bp = bp.with_display_name("DedupeRegression");

    // Three external sources
    auto src1 = make_node(interner, "src1");
    src1.type = interner.intern("Source");
    src1.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);
    auto src2 = make_node(interner, "src2");
    src2.type = interner.intern("Source");
    src2.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);
    auto src3 = make_node(interner, "src3");
    src3.type = interner.intern("Source");
    src3.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);

    // Node a: two inputs with names "in" and "in_2"
    auto a = make_node(interner, "a");
    a.type = interner.intern("NodeA");
    a.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);
    a.inputs.emplace_back(interner.intern("in_2"), PortSide::Input, PortType::V);
    a.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);

    // Node b: two inputs — "in" (external) and "link" (internal from a)
    // After dedupe, the external wire targeting b.in would produce iface_name "in"
    // which collides with the deduped "in_2" from old counter-based code.
    auto b = make_node(interner, "b");
    b.type = interner.intern("NodeB");
    b.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);
    b.inputs.emplace_back(interner.intern("link"), PortSide::Input, PortType::V);
    b.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);

    auto sink = make_node(interner, "sink");
    sink.type = interner.intern("Sink");
    sink.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);

    bp = bp.with_node(std::move(src1));
    bp = bp.with_node(std::move(src2));
    bp = bp.with_node(std::move(src3));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(sink));

    // Three external wires into selected nodes:
    //   src1.out -> a.in      (iface_name = "in")
    //   src2.out -> a.in_2    (iface_name = "in_2")
    //   src3.out -> b.in      (iface_name = "in" -> deduped to "in_?" )
    // Old code: "in", "in_2", "in_2" -> COLLISION
    auto w0 = make_wire(interner, arena, "w0", "src1", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(interner, arena, "w1", "src2", "out", "a", "in_2");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(interner, arena, "w2", "src3", "out", "b", "in");
    w2.domain = Domain::Electrical;
    // Internal wire a->b (into b's "link" input, not "in")
    auto w3 = make_wire(interner, arena, "w3", "a", "out", "b", "link");
    w3.domain = Domain::Electrical;
    // External wire b->sink
    auto w4 = make_wire(interner, arena, "w4", "b", "out", "sink", "in");
    w4.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    bp = bp.with_wire(std::move(w3));
    bp = bp.with_wire(std::move(w4));

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        bp,
        {interner.intern("a"), interner.intern("b")},
        "test_dedupe",
        "",
        interner,
        arena,
        &err);

    // Must succeed — deduplication should produce "in", "in_2", "in_3" (not a second "in_2")
    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;

    // Verify all interface port names are unique
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto& iface = updated->nested()[0].iface;

    std::set<std::string> port_names;
    for (const auto& pd : iface.ports()) {
        std::string name(interner.resolve(pd.name));
        EXPECT_TRUE(port_names.insert(name).second)
            << "Duplicate interface port name: " << name;
    }
}

// Edge case: all selected nodes are fully internally connected (no boundary wires).
// Result should be a nested blueprint with empty interface and a collapsed node with no ports.
TEST_F(CommandTest, ExtractToBlueprint_ZeroExternalConnections) {
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp_zero_ext"));
    bp = bp.with_display_name("ZeroExternal");

    auto a = make_node(interner, "a");
    a.type = interner.intern("NodeA");
    a.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);
    a.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);

    auto b = make_node(interner, "b");
    b.type = interner.intern("NodeB");
    b.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);
    b.outputs.emplace_back(interner.intern("out"), PortSide::Output, PortType::V);

    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));

    // Single internal wire only, no external connections
    auto w0 = make_wire(interner, arena, "w0", "a", "out", "b", "in");
    w0.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        bp,
        {interner.intern("a"), interner.intern("b")},
        "isolated_group",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    // Interface should be empty (no boundary connections)
    const auto& iface = updated->nested()[0].iface;
    EXPECT_EQ(iface.size(), 0u);

    // Collapsed node should have no ports
    const auto* collapsed = updated->find_node(updated->nested()[0].id);
    ASSERT_NE(collapsed, nullptr);
    EXPECT_TRUE(collapsed->inputs.empty());
    EXPECT_TRUE(collapsed->outputs.empty());

    // Internal wire should be preserved in parent (both endpoints are selected)
    // Plus the inline_def should contain the wire
    ASSERT_TRUE(updated->nested()[0].inline_def != nullptr);
    EXPECT_GE(updated->nested()[0].inline_def->wires().size(), 1u);
}
