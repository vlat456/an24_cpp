#include <gtest/gtest.h>

#include "editor/subwindow_open_target.h"
#include "ui/core/interned_id.h"

TEST(SubWindowOpenTarget, ResolvesNestedFirst) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("n1"),
        interner.intern("math/FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_nested(std::move(nested));

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.view.expandable = true;
    node.view.blueprint_path = "math/FirstOrderLag";
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "n1");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedWithoutBlueprintPathFailsLoudlyAsMissing) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("n_missing"),
        interner.intern("math/FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_nested(std::move(nested));

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n_missing");
    node.view.expandable = true;
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "n_missing");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesEmbeddedNestedKind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("n2"),
        interner.intern("math/FirstOrderLag"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_nested(std::move(nested));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "n2");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesExternalBlueprintPath) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("firstorderlag_1");
    node.view.expandable = true;
    node.view.blueprint_path = "math/FirstOrderLag";
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "firstorderlag_1");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ExternalReference);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, MissingForUnknownNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "missing");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(target.path.empty());
}

// With the variant design, an Embedded always has a non-null inline_def.
// The old "embedded=true but null inline_def" state is now structurally impossible.
// This test verifies that an embedded nested with an empty inline_def still resolves.
TEST(SubWindowOpenTarget, EmbeddedNestedWithEmptyInlineDefStillResolvesEmbedded) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("broken_embedded"),
        interner.intern("some/Type"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_nested(std::move(nested));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "broken_embedded");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);

    const auto* found = bp.find_nested(interner.intern("broken_embedded"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->is_embedded());
    EXPECT_NE(found->inline_def(), nullptr);  // always non-null with variant design
}

// Verify nested priority: nested lookup takes precedence over node lookup
// for both embedded and non-embedded cases.
TEST(SubWindowOpenTarget, EmbeddedNestedTakesPriorityOverExpandableNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("shared_id"),
        interner.intern("math/Adder"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_nested(std::move(nested));

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("shared_id");
    node.view.expandable = true;
    node.view.blueprint_path = "math/Adder";
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "shared_id");
    // Nested is checked first, and it's embedded → EmbeddedNested
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
}
