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
