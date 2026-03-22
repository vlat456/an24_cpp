#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"

TEST(EditorModel, EmptyByDefault) {
    bp2::EditorModel model;
    EXPECT_TRUE(model.current().nodes().empty());
    EXPECT_TRUE(model.current().wires().empty());
    EXPECT_TRUE(model.current().nested().empty());
}

TEST(EditorModel, ConstructWithBlueprint) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));
    bp2::EditorModel model(bp);
    EXPECT_EQ(interner.resolve(model.current().id()), "test");
}

TEST(EditorModel, AddNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");

    EXPECT_TRUE(model.add_node(std::move(node)));
    EXPECT_EQ(model.current().nodes().size(), 1u);
}

TEST(EditorModel, DuplicateNodeRejected) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.id = interner.intern("same_id");
    n1.type = interner.intern("Battery");
    n2.id = interner.intern("same_id");
    n2.type = interner.intern("Resistor");

    EXPECT_TRUE(model.add_node(std::move(n1)));
    EXPECT_FALSE(model.add_node(std::move(n2)));
    EXPECT_EQ(model.current().nodes().size(), 1u);
    EXPECT_EQ(model.undo_depth(), 1u);
}

TEST(EditorModel, RemoveNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    model.add_node(std::move(node));

    EXPECT_TRUE(model.remove_node(interner.intern("n1")));
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST(EditorModel, RemoveNonexistentNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;
    EXPECT_FALSE(model.remove_node(interner.intern("nope")));
}

TEST(EditorModel, AddWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.id = interner.intern("b1");
    n1.type = interner.intern("Battery");
    n2.id = interner.intern("r1");
    n2.type = interner.intern("Resistor");
    model.add_node(std::move(n1));
    model.add_node(std::move(n2));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("b1")),
        interner.intern("v_out"));
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in"));

    EXPECT_TRUE(model.add_wire(std::move(wire)));
    EXPECT_EQ(model.current().wires().size(), 1u);
}

TEST(EditorModel, RejectsSelfLoopWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    model.add_node(std::move(n1));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("w_self");
    auto port = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n1")),
        interner.intern("v_out"));
    w.source = port;
    w.target = port;  // self-loop

    EXPECT_FALSE(model.add_wire(std::move(w)));
}

TEST(EditorModel, AddNested) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;

    EXPECT_TRUE(model.add_nested(std::move(nested)));
    EXPECT_EQ(model.current().nested().size(), 1u);
}

TEST(EditorModel, DuplicateNestedRejected) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Nested n1, n2;
    n1.id = interner.intern("same_id");
    n1.blueprint_id = interner.intern("bp1");
    n2.id = interner.intern("same_id");
    n2.blueprint_id = interner.intern("bp2");

    EXPECT_TRUE(model.add_nested(std::move(n1)));
    EXPECT_FALSE(model.add_nested(std::move(n2)));
}

TEST(EditorModel, RemoveNested) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    model.add_nested(std::move(nested));

    EXPECT_TRUE(model.remove_nested(interner.intern("sub1")));
    EXPECT_EQ(model.current().nested().size(), 0u);
}

TEST(EditorModel, UndoRestoresPreviousState) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");

    model.add_node(std::move(node));
    EXPECT_EQ(model.current().nodes().size(), 1u);

    model.undo();
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST(EditorModel, RedoAfterUndo) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");

    model.add_node(std::move(node));
    model.undo();
    EXPECT_TRUE(model.can_redo());
    EXPECT_EQ(model.redo_depth(), 1u);

    model.redo();
    EXPECT_EQ(model.current().nodes().size(), 1u);
}

TEST(EditorModel, NewActionClearsRedoStack) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    n2.id = interner.intern("n2");
    n2.type = interner.intern("Resistor");

    model.add_node(std::move(n1));
    model.undo();
    model.add_node(std::move(n2));
    EXPECT_FALSE(model.can_redo());
    EXPECT_EQ(model.redo_depth(), 0u);
}

TEST(EditorModel, UpdateNodePosition) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    model.add_node(std::move(node));

    model.update_node_position(interner.intern("n1"), 300.0f, 400.0f);

    auto* found = model.current().find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->x, 300.0f);
    EXPECT_FLOAT_EQ(found->y, 400.0f);
}

TEST(EditorModel, UpdateNodePositionCreatesCheckpoint) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    model.add_node(std::move(node));

    EXPECT_EQ(model.undo_depth(), 1u);  // add_node checkpoint
    model.update_node_position(interner.intern("n1"), 50.0f, 60.0f);
    EXPECT_EQ(model.undo_depth(), 2u);  // update_node_position checkpoint

    model.undo();
    auto* found = model.current().find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->x, 0.0f);  // Original position
}

TEST(EditorModel, BakeNestedConvertsReferenceToEmbedded) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("sub_type"));
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    reg.register_blueprint(interner.intern("sub_type"), inner.iface(), "test", &inner);

    bp2::EditorModel model;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("sub_type");
    nested.embedded = false;
    nested.iface = inner.iface();
    model.add_nested(std::move(nested));

    EXPECT_TRUE(model.bake_nested(interner.intern("sub1"), reg, interner));

    auto* baked = model.current().find_nested(interner.intern("sub1"));
    ASSERT_NE(baked, nullptr);
    EXPECT_TRUE(baked->embedded);
    EXPECT_NE(baked->inline_def, nullptr);
}

TEST(EditorModel, BakeNestedIsUndoable) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("sub_type"));
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    reg.register_blueprint(interner.intern("sub_type"), inner.iface(), "test", &inner);

    bp2::EditorModel model;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("sub_type");
    nested.embedded = false;
    nested.iface = inner.iface();
    model.add_nested(std::move(nested));

    size_t depth_before_bake = model.undo_depth();
    EXPECT_TRUE(model.bake_nested(interner.intern("sub1"), reg, interner));

    // Bake must push a checkpoint so it's undoable
    EXPECT_EQ(model.undo_depth(), depth_before_bake + 1);

    auto* baked = model.current().find_nested(interner.intern("sub1"));
    ASSERT_NE(baked, nullptr);
    EXPECT_TRUE(baked->embedded);

    // Undo should restore the reference-mode nested
    model.undo();
    auto* restored = model.current().find_nested(interner.intern("sub1"));
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->embedded);
    EXPECT_EQ(restored->blueprint_id, interner.intern("sub_type"));
}

TEST(EditorModel, BakeNestedFailsForNonExistent) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bp2::EditorModel model;

    EXPECT_FALSE(model.bake_nested(interner.intern("nope"), reg, interner));
}

TEST(EditorModel, BakeNestedFailsForAlreadyEmbedded) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bp2::EditorModel model;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;  // Already embedded
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    model.add_nested(std::move(nested));

    EXPECT_FALSE(model.bake_nested(interner.intern("sub1"), reg, interner));
}

TEST(EditorModel, BakeNestedFailsForUnknownBlueprintId) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("unknown_type");
    nested.embedded = false;
    model.add_nested(std::move(nested));

    EXPECT_FALSE(model.bake_nested(interner.intern("sub1"), reg, interner));
}

TEST(EditorModel, CanUndoInitiallyFalse) {
    bp2::EditorModel model;
    EXPECT_FALSE(model.can_undo());
}

TEST(EditorModel, CanRedoInitiallyFalse) {
    bp2::EditorModel model;
    EXPECT_FALSE(model.can_redo());
}

TEST(EditorModel, UndoDoesNothingWhenEmpty) {
    bp2::EditorModel model;
    model.undo();
    EXPECT_TRUE(model.current().nodes().empty());
}

TEST(EditorModel, RedoDoesNothingWhenEmpty) {
    bp2::EditorModel model;
    model.redo();
    EXPECT_TRUE(model.current().nodes().empty());
}

TEST(EditorModel, NodesInRectReturnsOnlyContainedNodes) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    n1.x = 10.0f;
    n1.y = 10.0f;

    bp2::Blueprint::Node n2;
    n2.id = interner.intern("n2");
    n2.type = interner.intern("Resistor");
    n2.x = 100.0f;
    n2.y = 100.0f;

    model.add_node(std::move(n1));
    model.add_node(std::move(n2));

    bp2::Rect rect;
    rect.x_min = 0.0f;
    rect.y_min = 0.0f;
    rect.x_max = 50.0f;
    rect.y_max = 50.0f;

    auto inside = model.nodes_in_rect(rect);
    ASSERT_EQ(inside.size(), 1u);
    EXPECT_EQ(inside[0], interner.intern("n1"));
}

TEST(EditorModel, NodesInRectReflectsNodeMovement) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    n1.x = 10.0f;
    n1.y = 10.0f;
    model.add_node(std::move(n1));

    bp2::Rect rect;
    rect.x_min = 0.0f;
    rect.y_min = 0.0f;
    rect.x_max = 50.0f;
    rect.y_max = 50.0f;

    auto before = model.nodes_in_rect(rect);
    ASSERT_EQ(before.size(), 1u);

    model.update_node_position(interner.intern("n1"), 200.0f, 200.0f);
    auto after = model.nodes_in_rect(rect);
    EXPECT_TRUE(after.empty());
}

TEST(EditorModel, WireExistsTracksAddAndRemove) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node src;
    src.id = interner.intern("b1");
    src.type = interner.intern("Battery");

    bp2::Blueprint::Node dst;
    dst.id = interner.intern("r1");
    dst.type = interner.intern("Resistor");

    model.add_node(std::move(src));
    model.add_node(std::move(dst));

    bp2::Path source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("b1")),
        interner.intern("v_out")
    );
    bp2::Path target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in")
    );

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = source;
    wire.target = target;

    EXPECT_FALSE(model.wire_exists(source, target));
    EXPECT_TRUE(model.add_wire(std::move(wire)));
    EXPECT_TRUE(model.wire_exists(source, target));

    EXPECT_TRUE(model.remove_wire(interner.intern("w1")));
    EXPECT_FALSE(model.wire_exists(source, target));
}

TEST(EditorModel, IsDirtyAfterUndoStackTruncation) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    // Fill undo stack to exactly max_history_ (100)
    for (int i = 0; i < 100; ++i) {
        bp2::Blueprint::Node node;
        node.id = interner.intern(("a_" + std::to_string(i)).c_str());
        node.type = interner.intern("Battery");
        model.add_node(std::move(node));
    }

    // save_depth_ = 100
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());

    // One more add triggers truncation: erase front, push back.
    // Stack size goes 100 -> 99 -> 100. save_depth_ stays 100.
    // is_dirty() = (100 != 100) = false — BUG: saved state was evicted!
    bp2::Blueprint::Node extra;
    extra.id = interner.intern("extra");
    extra.type = interner.intern("Battery");
    model.add_node(std::move(extra));

    // The saved state (was at undo_stack_[0]) has been evicted.
    // Document should be dirty.
    EXPECT_TRUE(model.is_dirty());
}
