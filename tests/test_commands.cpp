#include <gtest/gtest.h>
#include "editor/commands/commands.h"
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

TEST_F(CommandTest, SetGridStepRecordsInverse) {
    bp.grid_step = 20.0f;
    
    Command inverse = execute(bp, cmd_set_grid_step(40.0f));
    
    EXPECT_FLOAT_EQ(bp.grid_step, 40.0f);
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* grid_cmd = std::get_if<CmdSetGridStep>(atomic);
    ASSERT_NE(grid_cmd, nullptr);
    EXPECT_FLOAT_EQ(grid_cmd->new_step, 20.0f);
}

TEST_F(CommandTest, MoveNodeRecordsInverse) {
    ui::InternedId node_id = bp.interner().intern("test_node");
    Node node;
    node.id = node_id;
    node.pos = Pt(10.0f, 20.0f);
    bp.add_node(Node{node});
    
    Pt new_pos(100.0f, 200.0f);
    Command inverse = execute(bp, cmd_move_node(node_id, new_pos));
    
    Node* n = bp.find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->pos.x, 100.0f);
    EXPECT_FLOAT_EQ(n->pos.y, 200.0f);
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* move_cmd = std::get_if<CmdMoveNode>(atomic);
    ASSERT_NE(move_cmd, nullptr);
    EXPECT_EQ(move_cmd->node_id, node_id);
    EXPECT_FLOAT_EQ(move_cmd->new_pos.x, 10.0f);
    EXPECT_FLOAT_EQ(move_cmd->new_pos.y, 20.0f);
}

TEST_F(CommandTest, AddRemoveNode) {
    ui::InternedId node_id = bp.interner().intern("node1");
    
    Node node;
    node.id = node_id;
    node.pos = Pt(0, 0);
    node.type_name = "Battery";
    
    Command inv1 = execute(bp, cmd_add_node(Node{node}));
    EXPECT_EQ(bp.nodes.size(), 1u);
    
    auto* atomic1 = std::get_if<AtomicCommand>(&inv1);
    ASSERT_NE(atomic1, nullptr);
    auto* rem_cmd = std::get_if<CmdRemoveNode>(atomic1);
    ASSERT_NE(rem_cmd, nullptr);
    EXPECT_EQ(rem_cmd->node_id, node_id);
    
    Command inv2 = execute(bp, inv1);
    EXPECT_EQ(bp.nodes.size(), 0u);
    
    auto* atomic2 = std::get_if<AtomicCommand>(&inv2);
    ASSERT_NE(atomic2, nullptr);
    auto* add_cmd = std::get_if<CmdAddNode>(atomic2);
    ASSERT_NE(add_cmd, nullptr);
}

TEST_F(CommandTest, UndoRedoRoundTrip) {
    UndoStack stack;
    bp.grid_step = 16.0f;
    
    Command inv1 = execute(bp, cmd_set_grid_step(32.0f));
    stack.push(std::move(inv1));
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
    EXPECT_TRUE(stack.can_undo());
    
    Command undo_cmd = stack.pop_undo();
    Command redo_inv = execute(bp, undo_cmd);
    stack.push_redo(std::move(redo_inv));
    EXPECT_FLOAT_EQ(bp.grid_step, 16.0f);
    EXPECT_TRUE(stack.can_redo());
    
    Command redo_cmd = stack.pop_redo();
    Command undo_inv2 = execute(bp, redo_cmd);
    stack.push(std::move(undo_inv2));
    EXPECT_FLOAT_EQ(bp.grid_step, 32.0f);
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST_F(CommandTest, SetNameRecordsInverse) {
    ui::InternedId node_id = bp.interner().intern("node1");
    Node node;
    node.id = node_id;
    node.name = "OriginalName";
    bp.add_node(Node{node});
    
    Command inverse = execute(bp, cmd_set_name(node_id, "NewName"));
    
    Node* n = bp.find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->name, "NewName");
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* name_cmd = std::get_if<CmdSetName>(atomic);
    ASSERT_NE(name_cmd, nullptr);
    EXPECT_EQ(name_cmd->node_id, node_id);
    EXPECT_EQ(name_cmd->new_name, "OriginalName");
}

TEST_F(CommandTest, SetNameUndoRedo) {
    ui::InternedId node_id = bp.interner().intern("node1");
    Node node;
    node.id = node_id;
    node.name = "OldName";
    bp.add_node(Node{node});
    
    UndoStack stack;
    
    Command inv = execute(bp, cmd_set_name(node_id, "NewName"));
    stack.push(std::move(inv));
    EXPECT_EQ(bp.find_node(node_id)->name, "NewName");
    
    Command undo_cmd = stack.pop_undo();
    execute(bp, undo_cmd);
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
    
    Command inverse = execute(bp, cmd_swap_bus_ports(I.intern("bus"), wire_a_id, wire_b_id));
    
    EXPECT_EQ(bp.wires[0].id, wire_b_id);
    EXPECT_EQ(bp.wires[1].id, wire_a_id);
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* swap_cmd = std::get_if<CmdSwapBusPorts>(atomic);
    ASSERT_NE(swap_cmd, nullptr);
    EXPECT_EQ(swap_cmd->wire_id_a, wire_a_id);
    EXPECT_EQ(swap_cmd->wire_id_b, wire_b_id);
}

TEST_F(CommandTest, SwapBusPortsIsOwnInverse) {
    auto& I = bp.interner();
    
    Node n; n.id = I.intern("n"); n.at(0, 0);
    bp.add_node(std::move(n));
    
    ui::InternedId wire_a_id = I.intern("wa");
    ui::InternedId wire_b_id = I.intern("wb");
    bp.add_wire(Wire::make(wire_a_id, wire_output(I.intern("n"), I.intern("out")), wire_input(I.intern("n"), I.intern("in"))));
    bp.add_wire(Wire::make(wire_b_id, wire_output(I.intern("n"), I.intern("out")), wire_input(I.intern("n"), I.intern("in2"))));
    
    UndoStack stack;
    
    stack.push(execute(bp, cmd_swap_bus_ports(I.intern("n"), wire_a_id, wire_b_id)));
    EXPECT_EQ(bp.wires[0].id, wire_b_id);
    EXPECT_EQ(bp.wires[1].id, wire_a_id);
    
    Command undo_cmd = stack.pop_undo();
    execute(bp, undo_cmd);
    EXPECT_EQ(bp.wires[0].id, wire_a_id);
    EXPECT_EQ(bp.wires[1].id, wire_b_id);
}

TEST_F(CommandTest, SetNameMissingNodeDoesNotCrash) {
    ui::InternedId ghost_id = bp.interner().intern("ghost");
    
    // Should not crash, just warn and return a safe inverse
    Command inverse = execute(bp, cmd_set_name(ghost_id, "NewName"));
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* name_cmd = std::get_if<CmdSetName>(atomic);
    ASSERT_NE(name_cmd, nullptr);
    EXPECT_EQ(name_cmd->new_name, "NewName") << "No-op inverse returns same name";
}

TEST_F(CommandTest, SwapBusPortsMissingWireDoesNotCrash) {
    auto& I = bp.interner();
    
    Node n; n.id = I.intern("n"); n.at(0, 0);
    bp.add_node(std::move(n));
    
    // Wire IDs that don't exist in the blueprint
    ui::InternedId ghost_a = I.intern("ghost_a");
    ui::InternedId ghost_b = I.intern("ghost_b");
    
    // Should not crash, just warn and return the same command
    Command inverse = execute(bp, cmd_swap_bus_ports(I.intern("n"), ghost_a, ghost_b));
    
    auto* atomic = std::get_if<AtomicCommand>(&inverse);
    ASSERT_NE(atomic, nullptr);
    auto* swap_cmd = std::get_if<CmdSwapBusPorts>(atomic);
    ASSERT_NE(swap_cmd, nullptr);
}
