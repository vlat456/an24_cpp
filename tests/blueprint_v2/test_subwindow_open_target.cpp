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
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::Nested);
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
