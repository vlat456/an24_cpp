#include <gtest/gtest.h>

#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/embedded_mutation.h"

TEST(EmbeddedEditingUndo, EmbeddedBlueprintUndoRedoRoundTrip) {
     ui::StringInterner interner;

     bp2::Blueprint root;

     bp2::Blueprint inline_bp;
     bp2::Blueprint::Node inner;
     inner.semantic.id = interner.intern("inner_node");
     inner.semantic.type = interner.intern("Battery");
     inner.layout.x = 10.0f;
     inner.layout.y = 20.0f;
     inline_bp = inline_bp.with_node(inner);

     bp2::Blueprint::Node collapsed;
     collapsed.semantic.id = interner.intern("comp_1");
     collapsed.semantic.type = interner.intern("FirstOrderLag");
collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
          bp2::Blueprint::Node::BlueprintSource::make_embedded(
          std::make_unique<bp2::Blueprint>(inline_bp.with_id(interner.intern("FirstOrderLag"))))
      };
      root = root.with_node(collapsed);

     bp2::EditorModel model(root);

     const auto* before_node = model.current().find_node(interner.intern("comp_1"));
     ASSERT_NE(before_node, nullptr);
     ASSERT_TRUE(before_node->is_blueprint_instance());
     ASSERT_TRUE(before_node->is_blueprint_instance());
     ASSERT_NE(before_node->blueprint_instance().source.inline_def(), nullptr);
     const auto* before_inner = before_node->blueprint_instance().source.inline_def()->find_node(interner.intern("inner_node"));
     ASSERT_NE(before_inner, nullptr);
     EXPECT_FLOAT_EQ(before_inner->layout.x, 10.0f);

     bp2::Blueprint::Node updated_node = *before_node;
     bp2::Blueprint::Node moved = *before_inner;
     moved.layout.x = 42.0f;
     moved.layout.y = 99.0f;

auto updated_inline = std::make_unique<bp2::Blueprint>(
          bp2::replace_node_preserve_order(*before_node->blueprint_instance().source.inline_def(), std::move(moved)));
      updated_node.blueprint_instance().source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
          std::make_unique<bp2::Blueprint>(std::move(*updated_inline).with_id(before_node->blueprint_instance().source.blueprint_id())));

     model.push_checkpoint();
     model.replace_current(bp2::replace_node_preserve_order(model.current(), std::move(updated_node)));

     const auto* after_node = model.current().find_node(interner.intern("comp_1"));
     ASSERT_NE(after_node, nullptr);
     ASSERT_TRUE(after_node->is_blueprint_instance());
     ASSERT_NE(after_node->blueprint_instance().source.inline_def(), nullptr);
     const auto* after_inner = after_node->blueprint_instance().source.inline_def()->find_node(interner.intern("inner_node"));
     ASSERT_NE(after_inner, nullptr);
     EXPECT_FLOAT_EQ(after_inner->layout.x, 42.0f);
     EXPECT_FLOAT_EQ(after_inner->layout.y, 99.0f);

     model.undo();
     const auto* undo_node = model.current().find_node(interner.intern("comp_1"));
     ASSERT_NE(undo_node, nullptr);
     ASSERT_TRUE(undo_node->is_blueprint_instance());
     ASSERT_NE(undo_node->blueprint_instance().source.inline_def(), nullptr);
     const auto* undo_inner = undo_node->blueprint_instance().source.inline_def()->find_node(interner.intern("inner_node"));
     ASSERT_NE(undo_inner, nullptr);
     EXPECT_FLOAT_EQ(undo_inner->layout.x, 10.0f);
     EXPECT_FLOAT_EQ(undo_inner->layout.y, 20.0f);

     model.redo();
     const auto* redo_node = model.current().find_node(interner.intern("comp_1"));
     ASSERT_NE(redo_node, nullptr);
     ASSERT_TRUE(redo_node->is_blueprint_instance());
     ASSERT_NE(redo_node->blueprint_instance().source.inline_def(), nullptr);
     const auto* redo_inner = redo_node->blueprint_instance().source.inline_def()->find_node(interner.intern("inner_node"));
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

// Regression: mutate_embedded called inside mutate_atomically must NOT
// push an extra checkpoint. This catches the bug where mutate_embedded()
// used push_checkpoint() instead of push_checkpoint_if_enabled(), causing
// double-checkpoint when called from within an atomic block (e.g. addBlueprint).
TEST(EmbeddedEditingUndo, MutateEmbeddedInsideAtomicBlockProducesSingleUndoEntry) {
    ui::StringInterner interner;

    // Build a root with an embedded blueprint instance containing one inner node.
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner_bp"));
    bp2::Blueprint::Node inner_node;
    inner_node.semantic.id = interner.intern("inner_1");
    inner_node.semantic.type = interner.intern("Resistor");
    inner_node.layout.x = 10.0f;
    inner = inner.with_node(inner_node);

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("group_1");
    host.semantic.type = interner.intern("Group");
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner))
    };

    bp2::Blueprint root;
    root = root.with_node(host);

    bp2::EditorModel model(root);
    model.clear_history();
    ASSERT_EQ(model.undo_depth(), 0u);

    // Simulate addBlueprint pattern: mutate_embedded inside mutate_atomically.
    const auto path = std::vector<ui::InternedId>{interner.intern("group_1")};

    model.mutate_atomically([&] {
        // Also do a root-level change alongside the embedded change.
        bp2::Blueprint::Node root_node;
        root_node.semantic.id = interner.intern("root_node");
        root_node.semantic.type = interner.intern("Battery");
        model.add_node(std::move(root_node));

        // Embedded mutation — this must NOT push an extra checkpoint.
        model.mutate_embedded(path,
            [&](const bp2::Blueprint& embedded) -> bp2::Blueprint {
                bp2::Blueprint::Node extra;
                extra.semantic.id = interner.intern("inner_2");
                extra.semantic.type = interner.intern("Switch");
                return embedded.with_node(extra);
            });
    });

    // Must produce exactly ONE undo entry, not two.
    ASSERT_EQ(model.undo_depth(), 1u)
        << "mutate_embedded inside mutate_atomically must not push extra checkpoint";

    // Verify both changes applied.
    EXPECT_NE(model.current().find_node(interner.intern("root_node")), nullptr);
    const auto* host_after = model.current().find_node(interner.intern("group_1"));
    ASSERT_NE(host_after, nullptr);
    ASSERT_TRUE(host_after->has_embedded_blueprint());
    const auto* inline_after = host_after->blueprint_instance().source.inline_def();
    ASSERT_NE(inline_after, nullptr);
    EXPECT_NE(inline_after->find_node(interner.intern("inner_1")), nullptr);
    EXPECT_NE(inline_after->find_node(interner.intern("inner_2")), nullptr);

    // Single undo reverts EVERYTHING.
    model.undo();
    EXPECT_EQ(model.current().find_node(interner.intern("root_node")), nullptr);
    const auto* host_undo = model.current().find_node(interner.intern("group_1"));
    ASSERT_NE(host_undo, nullptr);
    ASSERT_TRUE(host_undo->has_embedded_blueprint());
    const auto* inline_undo = host_undo->blueprint_instance().source.inline_def();
    ASSERT_NE(inline_undo, nullptr);
    EXPECT_NE(inline_undo->find_node(interner.intern("inner_1")), nullptr);
    EXPECT_EQ(inline_undo->find_node(interner.intern("inner_2")), nullptr);
}

// Standalone mutate_embedded (not inside atomic block) must produce one checkpoint.
TEST(EmbeddedEditingUndo, StandaloneMutateEmbeddedProducesOneCheckpoint) {
    ui::StringInterner interner;

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner_bp"));
    bp2::Blueprint::Node inner_node;
    inner_node.semantic.id = interner.intern("inner_1");
    inner_node.semantic.type = interner.intern("Resistor");
    inner = inner.with_node(inner_node);

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("group_1");
    host.semantic.type = interner.intern("Group");
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner))
    };

    bp2::Blueprint root;
    root = root.with_node(host);

    bp2::EditorModel model(root);
    model.clear_history();
    ASSERT_EQ(model.undo_depth(), 0u);

    const auto path = std::vector<ui::InternedId>{interner.intern("group_1")};
    const bp2::MutationResult mr = model.mutate_embedded(path,
        [&](const bp2::Blueprint& embedded) -> bp2::Blueprint {
            bp2::Blueprint::Node extra;
            extra.semantic.id = interner.intern("inner_2");
            extra.semantic.type = interner.intern("Switch");
            return embedded.with_node(extra);
        });

    EXPECT_EQ(mr, bp2::MutationResult::Changed);
    EXPECT_EQ(model.undo_depth(), 1u);
}
