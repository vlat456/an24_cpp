#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "json_parser/json_parser.h"
#include "editor/visual/persist.h"

#include <filesystem>

namespace fs = std::filesystem;

static std::string resolve_library_blueprint_path(const std::string& relative) {
    const std::vector<fs::path> candidates = {
        fs::path("library") / relative,
        fs::path("../library") / relative,
        fs::path("../../library") / relative,
        fs::path("../../../library") / relative,
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p.string();
        }
    }
    return (fs::path("library") / relative).string();
}

TEST(Issue23NestedInlineOnly, NoRootShadowNodesByDesign) {
    ui::StringInterner interner;

    bp2::Blueprint root;
    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("comp_1");
    collapsed.semantic.type = interner.intern("FirstOrderLag");
    collapsed.layout.layout_group = "";
    collapsed.view.expandable = true;
    root = root.with_node(collapsed);

    bp2::Blueprint inline_bp;
    bp2::Blueprint::Node inner;
    inner.semantic.id = interner.intern("inner_node");
    inner.semantic.type = interner.intern("Battery");
    inner.layout.layout_group = "";
    inline_bp = inline_bp.with_node(inner);

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp_1"),
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>(inline_bp));
    root = root.with_nested(std::move(nested));

    ASSERT_EQ(root.nodes().size(), 1u);
    ASSERT_EQ(root.nested().size(), 1u);
    ASSERT_NE(root.nested()[0].inline_def(), nullptr);
    ASSERT_EQ(root.nested()[0].inline_def()->nodes().size(), 1u);

    const std::string nested_id = std::string(interner.resolve(root.nested()[0].id));
    for (const auto& n : root.nodes()) {
        EXPECT_NE(n.layout.layout_group, nested_id);
    }
}

TEST(Issue23NestedInlineOnly, PersistRoundTripKeepsInlineDef) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");

    auto loaded_src = load_blueprint_from_file_validated(
        resolve_library_blueprint_path("math/FirstOrderLag.blueprint").c_str(),
        interner,
        arena,
        registry);
    ASSERT_TRUE(loaded_src.has_value());

    bp2::Blueprint root;
    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("comp_1");
    collapsed.semantic.type = interner.intern("FirstOrderLag");
    collapsed.view.expandable = true;
    collapsed.semantic.iface = loaded_src->iface();
    root = root.with_node(collapsed);

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp_1"),
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>(*loaded_src));
    root = root.with_nested(std::move(nested));

    fs::path tmp = fs::temp_directory_path();
    tmp /= "issue23_inline_roundtrip.blueprint";
    ASSERT_TRUE(save_blueprint_to_file(root, interner, arena, registry, tmp.c_str()));

    ui::StringInterner interner2;
    bp2::PathArena arena2(interner2);
    auto loaded = load_blueprint_from_file_validated(tmp.c_str(), interner2, arena2, registry);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->nodes().size(), 1u);
    ASSERT_EQ(loaded->nested().size(), 1u);
    ASSERT_NE(loaded->nested()[0].inline_def(), nullptr);
    ASSERT_GT(loaded->nested()[0].inline_def()->nodes().size(), 0u);

    fs::remove(tmp);
}

TEST(Issue23NestedInlineOnly, PersistRoundTripKeepsNestedOfNestedInlineDefs) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");

    auto loaded_src = load_blueprint_from_file_validated(
        resolve_library_blueprint_path("math/FirstOrderLag.blueprint").c_str(),
        interner,
        arena,
        registry);
    ASSERT_TRUE(loaded_src.has_value());

    bp2::Blueprint root;
    bp2::Blueprint::Node parent_collapsed;
    parent_collapsed.semantic.id = interner.intern("parent_comp");
    parent_collapsed.semantic.type = interner.intern("FirstOrderLag");
    parent_collapsed.view.expandable = true;
    parent_collapsed.semantic.iface = loaded_src->iface();
    root = root.with_node(parent_collapsed);

    bp2::Blueprint child_inline = *loaded_src;
    bp2::Blueprint::Node nested_collapsed;
    nested_collapsed.semantic.id = interner.intern("child_comp");
    nested_collapsed.semantic.type = interner.intern("FirstOrderLag");
    nested_collapsed.view.expandable = true;
    nested_collapsed.semantic.iface = loaded_src->iface();
    child_inline = child_inline.with_node(nested_collapsed);

    auto child_nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("child_comp"),
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>(*loaded_src));
    child_inline = child_inline.with_nested(std::move(child_nested));

    auto parent_nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("parent_comp"),
        interner.intern("FirstOrderLag"),
        std::make_unique<bp2::Blueprint>(std::move(child_inline)));
    root = root.with_nested(std::move(parent_nested));

    fs::path tmp = fs::temp_directory_path();
    tmp /= "issue23_inline_nested_roundtrip.blueprint";
    ASSERT_TRUE(save_blueprint_to_file(root, interner, arena, registry, tmp.c_str()));

    ui::StringInterner interner2;
    bp2::PathArena arena2(interner2);
    auto loaded = load_blueprint_from_file_validated(tmp.c_str(), interner2, arena2, registry);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->nested().size(), 1u);
    const auto& loaded_parent_nested = loaded->nested()[0];
    ASSERT_NE(loaded_parent_nested.inline_def(), nullptr);
    ASSERT_EQ(loaded_parent_nested.inline_def()->nested().size(), 1u);

    const auto& loaded_child_nested = loaded_parent_nested.inline_def()->nested()[0];
    ASSERT_NE(loaded_child_nested.inline_def(), nullptr);
    ASSERT_GT(loaded_child_nested.inline_def()->nodes().size(), 0u);

    fs::remove(tmp);
}
