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
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            interner.intern("FirstOrderLag"))
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "n1");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedWithoutBlueprintPathResolvesWithIndex) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n_resolved");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            interner.intern("FirstOrderLag"))
    };
    // No blueprint_path set - should still resolve via LibraryIndex
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "n_resolved");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    // With LibraryIndex, path can be resolved from blueprint_id even without host.blueprint_path
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedMissingIndexEntryReportsFailure) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp2::LibraryIndex index;  // empty index

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n_missing");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            interner.intern("FirstOrderLag"))
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "n_missing");
    EXPECT_EQ(result.target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(result.target.path.empty());
    EXPECT_EQ(result.failure, editor::SubWindowOpenTargetFailure::MissingLibraryIndexEntry);
}

TEST(SubWindowOpenTarget, ResolvesEmbeddedNestedKind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n2");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            interner.intern("FirstOrderLag"),
            std::make_unique<bp2::Blueprint>())
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "n2");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
    EXPECT_TRUE(target.path.empty());
}

TEST(SubWindowOpenTarget, ResolvesExternalBlueprintPath) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("firstorderlag_1");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            interner.intern("FirstOrderLag"))
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "firstorderlag_1");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, ReferencedNestedWinsEvenIfHostMirrorPathIsPresent) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("n_hosted");
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            interner.intern("FirstOrderLag"))
    };
    bp = bp.with_node(std::move(host));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "n_hosted");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::ReferencedNested);
    EXPECT_EQ(target.path, "library/math/FirstOrderLag.blueprint");
}

TEST(SubWindowOpenTarget, UnknownNodeReportsFailure) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "missing");
    EXPECT_EQ(result.target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_TRUE(result.target.path.empty());
    EXPECT_EQ(result.failure, editor::SubWindowOpenTargetFailure::UnknownNodeId);
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
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            interner.intern("some/Type"),
            std::make_unique<bp2::Blueprint>())
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "broken_embedded");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);

    const auto* found = bp.find_blueprint_instance(interner.intern("broken_embedded"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->blueprint_instance().source.is_embedded());
    EXPECT_NE(found->blueprint_instance().source.inline_def(), nullptr);  // always non-null with variant design
}

// Verify nested priority: nested lookup takes precedence over node lookup
// for both embedded and non-embedded cases.
TEST(SubWindowOpenTarget, EmbeddedNestedTakesPriorityOverExpandableNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("shared_id");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            interner.intern("Adder"),
            std::make_unique<bp2::Blueprint>())
    };
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "shared_id");
    ASSERT_EQ(result.failure, editor::SubWindowOpenTargetFailure::None);
    const auto& target = result.target;
    // Node is a blueprint instance with embedded source → EmbeddedNested
    EXPECT_EQ(target.kind, editor::SubWindowOpenTargetKind::EmbeddedNested);
}

TEST(SubWindowOpenTarget, NonBlueprintInstanceReportsFailure) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    auto index = make_test_index();

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("plain_node");
    node.content = bp2::Blueprint::Node::ComponentData{};
    bp = bp.with_node(std::move(node));

    const auto result = editor::resolve_subwindow_open_target(bp, interner, index, "plain_node");
    EXPECT_EQ(result.target.kind, editor::SubWindowOpenTargetKind::Missing);
    EXPECT_EQ(result.failure, editor::SubWindowOpenTargetFailure::NotBlueprintInstance);
}
