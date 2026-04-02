#include <gtest/gtest.h>

#include "editor/subwindow_open_target.h"
#include "ui/core/interned_id.h"

TEST(SubWindowOpenTarget, ResolvesNestedFirst) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("n1");
    nested.blueprint_id = interner.intern("math/FirstOrderLag");
    bp = bp.with_nested(std::move(nested));

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.expandable = true;
    node.blueprint_path = "math/FirstOrderLag";
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "n1");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesEmbeddedNestedKind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("n2");
    nested.blueprint_id = interner.intern("math/FirstOrderLag");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    bp = bp.with_nested(std::move(nested));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "n2");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesExternalBlueprintPath) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.id = interner.intern("firstorderlag_1");
    node.expandable = true;
    node.blueprint_path = "math/FirstOrderLag";
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

// Regression: embedded nested with embedded=true but null inline_def.
// resolve_subwindow_open_target must still return EmbeddedNested (based on
// the embedded flag), and callers must null-check inline_def before
// dereferencing for viewport data.
TEST(SubWindowOpenTarget, EmbeddedNestedWithNullInlineDefStillResolvesEmbedded) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("broken_embedded");
    nested.blueprint_id = interner.intern("some/Type");
    nested.embedded = true;
    // intentionally leave inline_def as nullptr (corrupt data)
    bp = bp.with_nested(std::move(nested));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "broken_embedded");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);

    // Caller must guard: inline_def can be null even when embedded=true
    const auto* found = bp.find_nested(interner.intern("broken_embedded"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->embedded);
    EXPECT_EQ(found->inline_def, nullptr);
}

// Verify nested priority: nested lookup takes precedence over node lookup
// for both embedded and non-embedded cases.
TEST(SubWindowOpenTarget, EmbeddedNestedTakesPriorityOverExpandableNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("shared_id");
    nested.blueprint_id = interner.intern("math/Adder");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    bp = bp.with_nested(std::move(nested));

    bp2::Blueprint::Node node;
    node.id = interner.intern("shared_id");
    node.expandable = true;
    node.blueprint_path = "math/Adder";
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, "shared_id");
    // Nested is checked first, and it's embedded → EmbeddedNested
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
}
