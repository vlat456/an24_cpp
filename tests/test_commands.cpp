#include <gtest/gtest.h>
#include "editor/commands/commands.h"
#include "editor/commands/transaction_guard.h"
#include "editor/commands/blueprint_checksum.h"
#include "editor/data/blueprint.h"
#include "editor/undo/undo_stack.h"

class CommandTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
    Blueprint bp;
};

TEST_F(CommandTest, BlueprintDefaults) {
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
}

TEST_F(CommandTest, UndoStackBasic) {
    UndoStack stack;
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST_F(CommandTest, SetGridStepMutates) {
    bp.grid_step = 20.0f;

    execute(bp, cmd_set_grid_step(40.0f));

    EXPECT_FLOAT_EQ(bp.grid_step, 40.0f);
}

TEST_F(CommandTest, MoveNodeMutates) {
    ui::InternedId node_id = bp.interner().intern("test_node");
    Node node;
    node.id = node_id;
    node.pos = Pt(10.0f, 20.0f);
    bp.add_node(Node{node});

    execute(bp, cmd_move_node(node_id, Pt(100.0f, 200.0f)));

    Node* n = bp.find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(n->pos.y, 200.0f);
}

TEST_F(CommandTest, AddRemoveNode) {
    ui::InternedId node_id = bp.interner().intern("node1");

    Node node;
    node.id = node_id;
    node.pos = Pt(0, 0);
    node.type_name = "Battery";

    execute(bp, cmd_add_node(Node{node}));
    EXPECT_EQ(bp.nodes.size(), 1u);

    execute(bp, cmd_remove_node(node_id));
    EXPECT_EQ(bp.nodes.size(), 0u);
}

TEST_F(CommandTest, UndoRedoRoundTrip) {
    UndoStack stack;
    bp.grid_step = 16.0f;

    // Snapshot + mutate
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
    EXPECT_TRUE(stack.can_undo());

    // Undo
    stack.undo(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_TRUE(stack.can_redo());

    // Redo
    stack.redo(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST_F(CommandTest, SetNameMutates) {
    ui::InternedId node_id = bp.interner().intern("node1");
    Node node;
    node.id = node_id;
    node.name = "OriginalName";
    bp.add_node(Node{node});

    execute(bp, cmd_set_name(node_id, "NewName"));

    Node* n = bp.find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->name, "NewName");
}

TEST_F(CommandTest, SetNameUndoRedo) {
    ui::InternedId node_id = bp.interner().intern("node1");
    Node node;
    node.id = node_id;
    node.name = "OldName";
    bp.add_node(Node{node});

    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_set_name(node_id, "NewName"));
    EXPECT_EQ(bp.find_node(node_id)->name, "NewName");

    // Undo
    stack.undo(bp);
    EXPECT_EQ(bp.find_node(node_id)->name, "OldName");
}

TEST_F(CommandTest, SwapBusPortsSwapsWires) {
    auto& I = bp.interner();

    Node bus; bus.id = I.intern("bus"); bus.type_name = "Bus"; bus.at(0, 0);
    bus.output(I.intern("v_out"));
    bp.add_node(std::move(bus));

    Node a; a.id = I.intern("a"); a.at(100, 0); a.output(I.intern("out"));
    bp.add_node(std::move(a));

    Node b; b.id = I.intern("b"); b.at(200, 0); b.input(I.intern("in"));
    bp.add_node(std::move(b));

    ui::InternedId wire_a_id = I.intern("wire_a");
    ui::InternedId wire_b_id = I.intern("wire_b");
    bp.add_wire(Wire::make(wire_a_id, wire_output(I.intern("a"), I.intern("out")), wire_input(I.intern("bus"), I.intern("v_out"))));
    bp.add_wire(Wire::make(wire_b_id, wire_output(I.intern("bus"), I.intern("v_out")), wire_input(I.intern("b"), I.intern("in"))));

    EXPECT_EQ(bp.wires[0].id, wire_a_id);
    EXPECT_EQ(bp.wires[1].id, wire_b_id);

    execute(bp, cmd_swap_bus_ports(I.intern("bus"), wire_a_id, wire_b_id));

    EXPECT_EQ(bp.wires[0].id, wire_b_id);
    EXPECT_EQ(bp.wires[1].id, wire_a_id);
}

TEST_F(CommandTest, SwapBusPortsUndoRedo) {
    auto& I = bp.interner();

    Node n; n.id = I.intern("n"); n.at(0, 0);
    bp.add_node(std::move(n));

    ui::InternedId wire_a_id = I.intern("wa");
    ui::InternedId wire_b_id = I.intern("wb");
    bp.add_wire(Wire::make(wire_a_id, wire_output(I.intern("n"), I.intern("out")), wire_input(I.intern("n"), I.intern("in"))));
    bp.add_wire(Wire::make(wire_b_id, wire_output(I.intern("n"), I.intern("out")), wire_input(I.intern("n"), I.intern("in2"))));

    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_swap_bus_ports(I.intern("n"), wire_a_id, wire_b_id));
    EXPECT_EQ(bp.wires[0].id, wire_b_id);
    EXPECT_EQ(bp.wires[1].id, wire_a_id);

    // Undo
    stack.undo(bp);
    EXPECT_EQ(bp.wires[0].id, wire_a_id);
    EXPECT_EQ(bp.wires[1].id, wire_b_id);
}

TEST_F(CommandTest, SetNameMissingNodeDoesNotCrash) {
    ui::InternedId ghost_id = bp.interner().intern("ghost");

    // Should not crash, just warn
    execute(bp, cmd_set_name(ghost_id, "NewName"));
}

TEST_F(CommandTest, SwapBusPortsMissingWireDoesNotCrash) {
    auto& I = bp.interner();

    Node n; n.id = I.intern("n"); n.at(0, 0);
    bp.add_node(std::move(n));

    // Wire IDs that don't exist in the blueprint
    ui::InternedId ghost_a = I.intern("ghost_a");
    ui::InternedId ghost_b = I.intern("ghost_b");

    // Should not crash, just warn
    execute(bp, cmd_swap_bus_ports(I.intern("n"), ghost_a, ghost_b));
}

// =============================================================================
// Multiple-command grouping tests (snapshot-based)
// =============================================================================

TEST_F(CommandTest, MultipleCommandsUndoAsGroup) {
    auto& I = bp.interner();
    ui::InternedId id_a = I.intern("a");
    ui::InternedId id_b = I.intern("b");

    Node a; a.id = id_a; a.pos = Pt(0, 0);
    Node b; b.id = id_b; b.pos = Pt(10, 10);
    bp.add_node(Node{a});
    bp.add_node(Node{b});

    UndoStack stack;

    // Single snapshot before both moves
    stack.snapshot(bp);
    execute(bp, cmd_move_node(id_a, Pt(100, 100)));
    execute(bp, cmd_move_node(id_b, Pt(200, 200)));

    // Verify both moved
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.x, 200.0f);

    // Undo — restores both
    stack.undo(bp);
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 0.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.y, 0.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.x, 10.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.y, 10.0f);

    // Redo — re-applies
    stack.redo(bp);
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.x, 200.0f);
}

TEST_F(CommandTest, MixedCommandsUndoAsGroup) {
    auto& I = bp.interner();
    ui::InternedId id = I.intern("node");

    Node n; n.id = id; n.pos = Pt(0, 0); n.name = "original";
    bp.add_node(Node{n});
    bp.grid_step = 16.0f;

    UndoStack stack;

    // Snapshot, then multiple mixed commands
    stack.snapshot(bp);
    execute(bp, cmd_move_node(id, Pt(50, 50)));
    execute(bp, cmd_set_name(id, "renamed"));
    execute(bp, cmd_set_grid_step(32.0f));

    EXPECT_FLOAT_EQ(bp.find_node(id)->pos.x, 50.0f);
    EXPECT_EQ(bp.find_node(id)->name, "renamed");
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);

    // Undo all at once
    stack.undo(bp);

    EXPECT_FLOAT_EQ(bp.find_node(id)->pos.x, 0.0f);
    EXPECT_EQ(bp.find_node(id)->name, "original");
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
}

TEST_F(CommandTest, NoCommandsNoUndoEntry) {
    bp.grid_step = 16.0f;

    UndoStack stack;
    // No snapshot, no commands
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_FALSE(stack.can_undo());
}

// =============================================================================
// TransactionGuard tests
// =============================================================================

TEST_F(CommandTest, TransactionGuardSingleCommand) {
    UndoStack stack;
    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.pos = Pt(0, 0);
    bp.add_node(Node{n});

    {
        TransactionGuard txn(bp, stack);
        txn.execute(cmd_move_node(id, Pt(42, 42)));
    }

    EXPECT_FLOAT_EQ(bp.find_node(id)->pos.x, 42.0f);
    EXPECT_TRUE(stack.can_undo());

    // Undo
    stack.undo(bp);
    EXPECT_FLOAT_EQ(bp.find_node(id)->pos.x, 0.0f);
}

TEST_F(CommandTest, TransactionGuardMultipleCommands) {
    UndoStack stack;
    auto& I = bp.interner();
    ui::InternedId id_a = I.intern("a");
    ui::InternedId id_b = I.intern("b");

    Node a; a.id = id_a; a.pos = Pt(0, 0);
    Node b; b.id = id_b; b.pos = Pt(10, 10);
    bp.add_node(Node{a});
    bp.add_node(Node{b});

    {
        TransactionGuard txn(bp, stack);
        txn.execute(cmd_move_node(id_a, Pt(100, 100)));
        txn.execute(cmd_move_node(id_b, Pt(200, 200)));
    }

    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.x, 200.0f);

    // Single undo should restore both
    EXPECT_TRUE(stack.can_undo());

    stack.undo(bp);
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 0.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_b)->pos.x, 10.0f);
}

TEST_F(CommandTest, TransactionGuardEmpty) {
    UndoStack stack;

    {
        TransactionGuard txn(bp, stack);
        // No commands
    }

    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());  // no redo pollution from empty transaction
}

TEST_F(CommandTest, TransactionGuardDiscard) {
    UndoStack stack;
    bp.grid_step = 16.0f;

    {
        TransactionGuard txn(bp, stack);
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    // discard() reverts the blueprint to pre-transaction state
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());  // discarded state must not leak to redo
}

TEST_F(CommandTest, TransactionGuardManualCommit) {
    UndoStack stack;
    bp.grid_step = 16.0f;

    {
        TransactionGuard txn(bp, stack);
        txn.execute(cmd_set_grid_step(32.0f));
        txn.commit();  // Explicit commit
        // Destructor should be idempotent
    }

    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
    EXPECT_TRUE(stack.can_undo());
}

// =============================================================================
// Dirty flag / save-point tests
// =============================================================================

TEST_F(CommandTest, DirtyFlagInitiallyClean) {
    UndoStack stack;
    EXPECT_FALSE(stack.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterSnapshot) {
    UndoStack stack;
    stack.snapshot(bp);
    EXPECT_TRUE(stack.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterMarkSaved) {
    UndoStack stack;
    stack.snapshot(bp);
    stack.mark_saved();
    EXPECT_FALSE(stack.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterUndoPastSavePoint) {
    UndoStack stack;
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    stack.mark_saved();
    EXPECT_FALSE(stack.is_dirty());

    stack.undo(bp);
    EXPECT_TRUE(stack.is_dirty());
}

TEST_F(CommandTest, DirtyFlagRedoRestoresSavePoint) {
    UndoStack stack;
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    stack.mark_saved();  // save point at undo_stack size=1

    stack.undo(bp);
    EXPECT_TRUE(stack.is_dirty());

    stack.redo(bp);
    EXPECT_FALSE(stack.is_dirty());
}

// =============================================================================
// Redo-stack preservation tests
// =============================================================================

TEST_F(CommandTest, RedoStackPreservedByUndo) {
    UndoStack stack;

    // Simulate 3 actions
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(48.0f));
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(64.0f));

    // Undo all 3
    stack.undo(bp);
    stack.undo(bp);
    stack.undo(bp);

    EXPECT_TRUE(stack.can_redo());
    EXPECT_FALSE(stack.can_undo());

    // Redo first
    stack.redo(bp);
    EXPECT_TRUE(stack.can_redo());
    EXPECT_TRUE(stack.can_undo());

    // Redo second
    stack.redo(bp);
    EXPECT_TRUE(stack.can_redo());

    // Redo third
    stack.redo(bp);
    EXPECT_FALSE(stack.can_redo());
    EXPECT_TRUE(stack.can_undo());
}

TEST_F(CommandTest, NewSnapshotClearsRedo) {
    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));

    stack.undo(bp);
    EXPECT_TRUE(stack.can_redo());

    // New snapshot should clear redo
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(48.0f));
    EXPECT_FALSE(stack.can_redo());
}

// =============================================================================
// Blueprint checksum / undo round-trip verification tests
// =============================================================================

TEST_F(CommandTest, ChecksumDeterministic) {
    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.pos = Pt(10, 20); n.name = "test";
    bp.add_node(Node{n});

    size_t h1 = blueprint_checksum(bp);
    size_t h2 = blueprint_checksum(bp);
    EXPECT_EQ(h1, h2);
}

TEST_F(CommandTest, ChecksumChangesOnMutation) {
    size_t h1 = blueprint_checksum(bp);

    bp.grid_step = 999.0f;
    size_t h2 = blueprint_checksum(bp);
    EXPECT_NE(h1, h2);
}

TEST_F(CommandTest, UndoRoundTripChecksumMoveNode) {
    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.pos = Pt(10, 20);
    bp.add_node(Node{n});

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    stack.snapshot(bp);
    execute(bp, cmd_move_node(id, Pt(100, 200)));
    EXPECT_NE(blueprint_checksum(bp), before);  // State changed

    stack.undo(bp);  // Undo
    EXPECT_EQ(blueprint_checksum(bp), before);  // State restored
}

TEST_F(CommandTest, UndoRoundTripChecksumSetParam) {
    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.params["key"] = "old";
    bp.add_node(Node{n});

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    stack.snapshot(bp);
    execute(bp, cmd_set_param(id, "key", "new"));
    stack.undo(bp);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveNode) {
    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.pos = Pt(0, 0); n.type_name = "Test";

    stack.snapshot(bp);
    execute(bp, cmd_add_node(Node{n}));
    EXPECT_NE(blueprint_checksum(bp), before);

    stack.undo(bp);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumSetGridStep) {
    bp.grid_step = 16.0f;
    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(64.0f));
    stack.undo(bp);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumMixedCommands) {
    auto& I = bp.interner();
    ui::InternedId id_a = I.intern("a");
    ui::InternedId id_b = I.intern("b");

    Node a; a.id = id_a; a.pos = Pt(0, 0); a.name = "aaa";
    Node b; b.id = id_b; b.pos = Pt(10, 10); b.name = "bbb";
    bp.add_node(Node{a});
    bp.add_node(Node{b});
    bp.grid_step = 16.0f;

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    stack.snapshot(bp);
    execute(bp, cmd_move_node(id_a, Pt(100, 100)));
    execute(bp, cmd_set_name(id_b, "changed"));
    execute(bp, cmd_set_grid_step(32.0f));
    EXPECT_NE(blueprint_checksum(bp), before);

    stack.undo(bp);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveWire) {
    auto& I = bp.interner();
    ui::InternedId nid = I.intern("n");
    Node n; n.id = nid; n.pos = Pt(0, 0);
    n.output(I.intern("out"));
    n.input(I.intern("in"));
    bp.add_node(Node{n});

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    ui::InternedId wid = I.intern("w1");
    Wire w = Wire::make(wid, wire_output(nid, I.intern("out")), wire_input(nid, I.intern("in")));

    stack.snapshot(bp);
    execute(bp, cmd_add_wire(std::move(w)));
    EXPECT_EQ(bp.wires.size(), 1u);

    stack.undo(bp);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumRemoveNodeWithWires) {
    auto& I = bp.interner();
    ui::InternedId id_a = I.intern("a");
    ui::InternedId id_b = I.intern("b");

    Node a; a.id = id_a; a.pos = Pt(0, 0); a.output(I.intern("out"));
    Node b; b.id = id_b; b.pos = Pt(100, 0); b.input(I.intern("in"));
    bp.add_node(Node{a});
    bp.add_node(Node{b});

    ui::InternedId wid = I.intern("w1");
    bp.add_wire(Wire::make(wid, wire_output(id_a, I.intern("out")), wire_input(id_b, I.intern("in"))));

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    // Remove node 'a' — should also remove its wire
    stack.snapshot(bp);
    execute(bp, cmd_remove_node(id_a));
    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.wires.size(), 0u);

    // Undo — should restore node and wire
    stack.undo(bp);
    EXPECT_EQ(bp.nodes.size(), 2u);
    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(blueprint_checksum(bp), before);
}

TEST_F(CommandTest, ChecksumDeterministicWithMultipleParams) {
    auto& I = bp.interner();
    ui::InternedId id = I.intern("n");
    Node n; n.id = id; n.pos = Pt(0, 0); n.type_name = "Test";
    // Multiple params — unordered_map iteration order is non-deterministic
    n.params["zzz_last"] = "3";
    n.params["aaa_first"] = "1";
    n.params["mmm_middle"] = "2";
    bp.add_node(Node{n});

    UndoStack stack;
    size_t before = blueprint_checksum(bp);

    // Remove and undo (may rehash unordered_map to different bucket layout)
    stack.snapshot(bp);
    execute(bp, cmd_remove_node(id));
    stack.undo(bp);

    EXPECT_EQ(blueprint_checksum(bp), before)
        << "Checksum must be stable after remove+undo (param iteration order)";
}

// =============================================================================
// Regression tests — edge cases from code review
// =============================================================================

TEST_F(CommandTest, MaxStackDepthEviction) {
    UndoStack stack;
    bp.grid_step = 0.0f;

    // Push 101 snapshots — the oldest (snapshot #1) will be evicted
    for (int i = 1; i <= 101; ++i) {
        stack.snapshot(bp);
        execute(bp, cmd_set_grid_step(static_cast<float>(i)));
    }
    // i=1: snapshot saves grid=0.0f, execute sets grid=1.0f
    // i=2: snapshot saves grid=1.0f, execute sets grid=2.0f
    // ...
    // i=101: snapshot saves grid=100.0f, execute sets grid=101.0f
    // After 101 pushes, oldest (grid=0.0f) is evicted. Remaining: grid=1..100
    EXPECT_FLOAT_EQ(bp.grid_step, 101.0f);
    EXPECT_TRUE(stack.can_undo());

    // Undo all 100 remaining entries
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(stack.undo(bp));
    }
    EXPECT_FALSE(stack.can_undo());
    // Oldest remaining snapshot had grid=1.0f (saved at i=2)
    EXPECT_FLOAT_EQ(bp.grid_step, 1.0f);
}

TEST_F(CommandTest, SavePointEvictionMakesPermanentlyDirty) {
    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(1.0f));
    stack.mark_saved();  // save_point = 1
    EXPECT_FALSE(stack.is_dirty());

    // Push 100 more snapshots to evict the save point
    for (int i = 2; i <= 101; ++i) {
        stack.snapshot(bp);
        execute(bp, cmd_set_grid_step(static_cast<float>(i)));
    }
    // save_point was at index 0 after decrements, now evicted
    EXPECT_TRUE(stack.is_dirty());

    // mark_saved can fix it
    stack.mark_saved();
    EXPECT_FALSE(stack.is_dirty());
}

TEST_F(CommandTest, UndoRedoWithEmptyBlueprint) {
    UndoStack stack;
    // bp starts empty (no nodes, no wires)
    size_t before = blueprint_checksum(bp);

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));

    stack.undo(bp);
    EXPECT_EQ(blueprint_checksum(bp), before);
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);

    stack.redo(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
}

TEST_F(CommandTest, TransactionGuardDiscardMultipleCommands) {
    UndoStack stack;
    auto& I = bp.interner();
    ui::InternedId id_a = I.intern("a");
    ui::InternedId id_b = I.intern("b");

    Node a; a.id = id_a; a.pos = Pt(0, 0); a.name = "aaa";
    Node b; b.id = id_b; b.pos = Pt(10, 10); b.name = "bbb";
    bp.add_node(Node{a});
    bp.add_node(Node{b});
    bp.grid_step = 16.0f;

    size_t before = blueprint_checksum(bp);

    {
        TransactionGuard txn(bp, stack);
        txn.execute(cmd_move_node(id_a, Pt(100, 100)));
        txn.execute(cmd_set_name(id_b, "changed"));
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    EXPECT_EQ(blueprint_checksum(bp), before);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());  // discarded state must not leak to redo
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_FLOAT_EQ(bp.find_node(id_a)->pos.x, 0.0f);
    EXPECT_EQ(bp.find_node(id_b)->name, "bbb");
}

TEST_F(CommandTest, ClearResetsEverything) {
    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(48.0f));
    stack.mark_saved();

    // Undo one to get a redo entry
    stack.undo(bp);
    EXPECT_TRUE(stack.can_undo());
    EXPECT_TRUE(stack.can_redo());
    EXPECT_TRUE(stack.is_dirty());

    stack.clear();
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_FALSE(stack.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterUndoRedoNewSnapshot) {
    UndoStack stack;

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));
    stack.mark_saved();  // save_point = 1
    EXPECT_FALSE(stack.is_dirty());

    // Undo past save point
    stack.undo(bp);
    EXPECT_TRUE(stack.is_dirty());

    // Redo back to save point
    stack.redo(bp);
    EXPECT_FALSE(stack.is_dirty());

    // New snapshot past save point
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(64.0f));
    EXPECT_TRUE(stack.is_dirty());
}

TEST_F(CommandTest, DiscardLastSnapshotClean) {
    UndoStack stack;
    bp.grid_step = 16.0f;

    stack.snapshot(bp);
    // Simulate a no-op mutation (grid_step unchanged in reality)
    bp.grid_step = 16.0f;

    // Discard without polluting redo
    stack.discard_last_snapshot();

    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
}

TEST_F(CommandTest, DiscardLastSnapshotDoesNotAffectRedo) {
    UndoStack stack;

    // Create a real undo entry first
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(32.0f));

    // Now snapshot for a second "mutation" that turns out to be a no-op
    stack.snapshot(bp);
    // Oops, no actual change — discard
    stack.discard_last_snapshot();

    // The first undo entry should still be intact
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());

    stack.undo(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_FALSE(stack.can_undo());
}

TEST_F(CommandTest, EmptyTransactionPreservesExistingRedo) {
    UndoStack stack;

    // Create a real entry, then undo to get a redo entry
    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(64.0f));
    stack.undo(bp);
    EXPECT_TRUE(stack.can_redo());
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);

    // An empty transaction guard should not disturb the existing redo entry
    {
        TransactionGuard txn(bp, stack);
        // No commands
    }

    EXPECT_FALSE(stack.can_undo());
    EXPECT_TRUE(stack.can_redo());  // existing redo entry preserved

    stack.redo(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 64.0f);
}

TEST_F(CommandTest, RestoreLastSnapshotRoundTrip) {
    UndoStack stack;
    bp.grid_step = 16.0f;

    stack.snapshot(bp);
    execute(bp, cmd_set_grid_step(99.0f));
    EXPECT_FLOAT_EQ(bp.grid_step, 99.0f);

    // Restore without touching redo
    stack.restore_last_snapshot(bp);
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());  // redo must be untouched
}
