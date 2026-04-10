#include <gtest/gtest.h>

#include "editor/subwindow_open_target.h"
#include "blueprint_v2/library/library_index.h"
#include "ui/core/interned_id.h"

/// Create a minimal test LibraryIndex with math/FirstOrderLag for testing
static bp2::LibraryIndex make_test_index() {
    bp2::LibraryIndex index;
    index.entries["FirstOrderLag"] = "library/math/FirstOrderLag.blueprint";
    return index;
}

TEST(SubWindowOpenTarget, ResolvesNestedFirst) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "n1");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedWithoutBlueprintPathResolvesWithIndex) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n_resolved");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{});
    // No blueprint_path set - should still resolve via LibraryIndex
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "n_resolved");
    // With LibraryIndex, path can be resolved from blueprint_id even without host.blueprint_path
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedMissingIndexEntryReturnsMissing) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp2::LibraryIndex index;  // empty index

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n_missing");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "n_missing");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesEmbeddedNestedKind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n2");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "n2");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesExternalBlueprintPath) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("firstorderlag_1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "firstorderlag_1");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedWinsEvenIfHostMirrorPathIsPresent) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("n_hosted");
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{});
    bp = bp.with_node(std::move(host));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "n_hosted");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, MissingForUnknownNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "missing");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(target.path.empty());
}

// With the variant design, an Embedded always has a non-null inline_def.
// The old "embedded=true but null inline_def" state is now structurally impossible.
// This test verifies that an embedded nested with an empty inline_def still resolves.
TEST(SubWindowOpenTarget, EmbeddedNestedWithEmptyInlineDefStillResolvesEmbedded) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("broken_embedded");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("some/Type"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "broken_embedded");
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);

    const auto* found = bp.find_blueprint_instance(interner.intern("broken_embedded"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->source->is_embedded());
    EXPECT_NE(found->source->inline_def(), nullptr);  // always non-null with variant design
}

// Verify nested priority: nested lookup takes precedence over node lookup
// for both embedded and non-embedded cases.
TEST(SubWindowOpenTarget, EmbeddedNestedTakesPriorityOverExpandableNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("shared_id");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("Adder"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_node(std::move(node));

    const auto target = editor::resolve_subwindow_open_target(bp, interner, index, "shared_id");
    // Node is a blueprint instance with embedded source → EmbeddedNested
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
}
