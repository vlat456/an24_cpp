#include <gtest/gtest.h>

#include "blueprint_v2/editor_model/editor_model.h"

TEST(EmbeddedEditingUndo, NestedInlineDefUndoRedoRoundTrip) {
    ui::StringInterner interner;

    bp2::Blueprint root;

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("comp_1");
    collapsed.semantic.type = interner.intern("FirstOrderLag");
    collapsed.view.expandable = true;
    root = root.with_node(collapsed);

    bp2::Blueprint inline_bp;
    bp2::Blueprint::Node inner;
    inner.semantic.id = interner.intern("inner_node");
    inner.semantic.type = interner.intern("Battery");
    inner.layout.x = 10.0f;
    inner.layout.y = 20.0f;
    inline_bp = inline_bp.with_node(inner);

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp_1"),
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>(inline_bp));
    root = root.with_nested(std::move(nested));

    bp2::EditorModel model(root);

    const auto* before_nested = model.current().find_nested(interner.intern("comp_1"));
    ASSERT_NE(before_nested, nullptr);
    ASSERT_NE(before_nested->inline_def(), nullptr);
    const auto* before_inner = before_nested->inline_def()->find_node(interner.intern("inner_node"));
    ASSERT_NE(before_inner, nullptr);
    EXPECT_FLOAT_EQ(before_inner->layout.x, 10.0f);

    bp2::Blueprint::Nested updated = *before_nested;
    bp2::Blueprint::Node moved = *before_inner;
    moved.layout.x = 42.0f;
    moved.layout.y = 99.0f;
    updated.set_inline_def(std::make_unique<bp2::Blueprint>(
        bp2::replace_node_preserve_order(*before_nested->inline_def(), std::move(moved))));

    model.push_checkpoint();
    model.replace_current(bp2::replace_nested_preserve_order(model.current(), std::move(updated)));

    const auto* after_nested = model.current().find_nested(interner.intern("comp_1"));
    ASSERT_NE(after_nested, nullptr);
    ASSERT_NE(after_nested->inline_def(), nullptr);
    const auto* after_inner = after_nested->inline_def()->find_node(interner.intern("inner_node"));
    ASSERT_NE(after_inner, nullptr);
    EXPECT_FLOAT_EQ(after_inner->layout.x, 42.0f);
    EXPECT_FLOAT_EQ(after_inner->layout.y, 99.0f);

    model.undo();
    const auto* undo_nested = model.current().find_nested(interner.intern("comp_1"));
    ASSERT_NE(undo_nested, nullptr);
    ASSERT_NE(undo_nested->inline_def(), nullptr);
    const auto* undo_inner = undo_nested->inline_def()->find_node(interner.intern("inner_node"));
    ASSERT_NE(undo_inner, nullptr);
    EXPECT_FLOAT_EQ(undo_inner->layout.x, 10.0f);
    EXPECT_FLOAT_EQ(undo_inner->layout.y, 20.0f);

    model.redo();
    const auto* redo_nested = model.current().find_nested(interner.intern("comp_1"));
    ASSERT_NE(redo_nested, nullptr);
    ASSERT_NE(redo_nested->inline_def(), nullptr);
    const auto* redo_inner = redo_nested->inline_def()->find_node(interner.intern("inner_node"));
    ASSERT_NE(redo_inner, nullptr);
    EXPECT_FLOAT_EQ(redo_inner->layout.x, 42.0f);
    EXPECT_FLOAT_EQ(redo_inner->layout.y, 99.0f);
}

// Regression: nested mutate_atomically must not produce extra undo entries.
// This catches the bug where EmbeddedInlineHost methods each wrap in
// root_model_.mutate_atomically(), causing double-checkpoints when called
// from within an outer mutate_atomically() block.
TEST(EmbeddedEditingUndo, NestedMutateAtomicallyProducesSingleUndoEntry) {
    bp2::EditorModel model;
    ui::StringInterner interner;

    // Seed with two nodes so we can move them both in one outer transaction
    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("a");
    n1.semantic.type = interner.intern("T");
    n1.layout.x = 0.0f;

    bp2::Blueprint::Node n2;
    n2.semantic.id = interner.intern("b");
    n2.semantic.type = interner.intern("T");
    n2.layout.x = 0.0f;

    model.add_node(std::move(n1));
    model.add_node(std::move(n2));
    model.clear_history();

    const size_t undo_before = model.undo_depth();
    ASSERT_EQ(undo_before, 0u);

    // Outer mutate_atomically with inner mutate_atomically calls (simulates
    // EmbeddedInlineHost pattern where each mutation wraps in mutate_atomically)
    model.mutate_atomically([&] {
        model.mutate_atomically([&] {
            model.update_node_position(interner.intern("a"), 10.0f, 0.0f);
        });
        model.mutate_atomically([&] {
            model.update_node_position(interner.intern("b"), 20.0f, 0.0f);
        });
    });

    // Must produce exactly ONE undo entry, not three
    ASSERT_EQ(model.undo_depth(), 1u)
        << "Nested mutate_atomically must produce a single undo checkpoint";

    // Verify both changes were applied
    EXPECT_FLOAT_EQ(model.current().find_node(interner.intern("a"))->layout.x, 10.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(interner.intern("b"))->layout.x, 20.0f);

    // Single undo reverts BOTH
    model.undo();
    EXPECT_FLOAT_EQ(model.current().find_node(interner.intern("a"))->layout.x, 0.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(interner.intern("b"))->layout.x, 0.0f);
}

// Regression: nested mutate_atomically with no-op inner call should still
// report the outer change correctly (no false negative).
TEST(EmbeddedEditingUndo, NestedMutateAtomicallyNoOpInnerStillReportsOuterChange) {
    bp2::EditorModel model;
    ui::StringInterner interner;

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("x");
    n.semantic.type = interner.intern("T");
    n.layout.x = 0.0f;
    model.add_node(std::move(n));
    model.clear_history();

    bool outer_result = model.mutate_atomically([&] {
        // Inner no-op: moves node to same position
        model.mutate_atomically([&] {
            model.update_node_position(interner.intern("x"), 0.0f, 0.0f);
        });
        // Actual change
        model.update_node_position(interner.intern("x"), 5.0f, 0.0f);
    });

    EXPECT_TRUE(outer_result);
    ASSERT_EQ(model.undo_depth(), 1u);
    EXPECT_FLOAT_EQ(model.current().find_node(interner.intern("x"))->layout.x, 5.0f);
}

// Regression: fully no-op nested mutate_atomically should return false and
// not leave a stale checkpoint on the undo stack.
TEST(EmbeddedEditingUndo, NestedMutateAtomicallyFullNoOpReturnsFalse) {
    bp2::EditorModel model;
    ui::StringInterner interner;

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("x");
    n.semantic.type = interner.intern("T");
    n.layout.x = 5.0f;
    model.add_node(std::move(n));
    model.clear_history();

    bool result = model.mutate_atomically([&] {
        model.mutate_atomically([&] {
            // no-op: same position
            model.update_node_position(interner.intern("x"), 5.0f, 0.0f);
        });
    });

    EXPECT_FALSE(result);
    EXPECT_EQ(model.undo_depth(), 0u)
        << "Full no-op nested mutate_atomically must not leave stale checkpoint";
}
