#include <gtest/gtest.h>

#include "editor/document.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/library/library_index.h"
#include "json_parser/json_parser.h"

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_dir(const char* name) {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open()) << path;
    out << contents;
}

const bp2::Blueprint::Node* require_node(const bp2::Blueprint& bp, ui::StringInterner& interner, const char* id) {
    auto* node = bp.find_node(interner.lookup(id));
    EXPECT_NE(node, nullptr) << id;
    return node;
}

} // namespace

TEST(DocumentSafety, AddComponentUnknownTypeDoesNotCrashOrMutate) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");

    const size_t before_nodes = doc.model().current().nodes().size();
    const size_t before_wires = doc.model().current().wires().size();

    EXPECT_NO_THROW(doc.addComponent("DefinitelyUnknownComponent", Pt{64.0f, 64.0f}, "", registry));

    EXPECT_EQ(doc.model().current().nodes().size(), before_nodes);
    EXPECT_EQ(doc.model().current().wires().size(), before_wires);
}

TEST(DocumentSafety, LoadHydratesRootNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_root");
    const fs::path bp_path = dir / "root.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("root_hydrate"));
    bp = bp.with_display_name("Root Hydrate");

    bp2::Blueprint::Node slider;
    slider.semantic.id = interner.intern("slider1");
    slider.semantic.type = interner.intern("Slider");
    slider.layout.x = 10.0f;
    slider.layout.y = 20.0f;

    bp2::Blueprint::Node value;
    value.semantic.id = interner.intern("value1");
    value.semantic.type = interner.intern("Value");
    value.layout.x = 40.0f;
    value.layout.y = 60.0f;

    bp = bp.with_node(std::move(slider));
    bp = bp.with_node(std::move(value));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded_slider = require_node(doc.model().current(), doc.interner(), "slider1");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_max, 1.0f);

    const auto* loaded_value = require_node(doc.model().current(), doc.interner(), "value1");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(loaded_value->view.render_hint, "ref");

    fs::remove_all(dir);
}

TEST(DocumentSafety, LoadHydratesEmbeddedInlineBlueprintNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_embedded");
    const fs::path bp_path = dir / "embedded.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inline_bp;
    inline_bp = inline_bp.with_id(interner.intern("inline_bp"));
    inline_bp = inline_bp.with_display_name("Inline BP");

    bp2::Blueprint::Node slider;
    slider.semantic.id = interner.intern("inner_slider");
    slider.semantic.type = interner.intern("Slider");
    slider.layout.x = 4.0f;
    slider.layout.y = 8.0f;
    inline_bp = inline_bp.with_node(std::move(slider));

    bp2::Blueprint::Node host;
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.semantic.id = interner.intern("host1");
    host.semantic.type = interner.intern("Group");
    host.layout.collapsed = true;
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("Group"),
        std::make_unique<bp2::Blueprint>(inline_bp));

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root_embedded_hydrate"));
    root = root.with_display_name("Embedded Hydrate");
    root = root.with_node(std::move(host));

    write_file(bp_path, bp2::BlueprintCodec::encode(root, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded_host = require_node(doc.model().current(), doc.interner(), "host1");
    ASSERT_NE(loaded_host, nullptr);
    ASSERT_TRUE(loaded_host->source.has_value());
    ASSERT_TRUE(loaded_host->source->is_embedded());
    ASSERT_NE(loaded_host->source->inline_def(), nullptr);

    const auto* loaded_slider = require_node(*loaded_host->source->inline_def(), doc.interner(), "inner_slider");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_max, 1.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, OpenExternalRefWindowHydratesNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    bp2::LibraryIndex index;
    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_external");
    const fs::path ext_path = dir / "external.blueprint";
    index.entries["external_test"] = ext_path.string();
    doc.setLibraryIndex(&index);

    ui::StringInterner ext_interner;
    bp2::PathArena ext_arena(ext_interner);
    bp2::Blueprint ext_bp;
    ext_bp = ext_bp.with_id(ext_interner.intern("external_test"));
    ext_bp = ext_bp.with_display_name("External Test");

    bp2::Blueprint::Node slider;
    slider.semantic.id = ext_interner.intern("external_slider");
    slider.semantic.type = ext_interner.intern("Slider");

    bp2::Blueprint::Node value;
    value.semantic.id = ext_interner.intern("external_value");
    value.semantic.type = ext_interner.intern("Value");

    ext_bp = ext_bp.with_node(std::move(slider));
    ext_bp = ext_bp.with_node(std::move(value));

    write_file(ext_path, bp2::BlueprintCodec::encode(ext_bp, ext_interner, ext_arena, &registry));

    bp2::Blueprint::Node ref_host;
    ref_host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    ref_host.semantic.id = doc.interner().intern("external_node");
    ref_host.semantic.type = doc.interner().intern("external_test");
    ref_host.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        doc.interner().intern("external_test"),
        bp2::Interface{});
    doc.model().replace_current(doc.model().current().with_node(std::move(ref_host)));

    doc.openSubWindow("external_node");

    auto* win = doc.windowManager().find(WindowScopeId::external("external_node"));
    ASSERT_NE(win, nullptr);
    ASSERT_TRUE(win->external_blueprint.has_value());

    const auto* loaded_slider = require_node(*win->external_blueprint, *win->external_interner, "external_slider");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);

    const auto* loaded_value = require_node(*win->external_blueprint, *win->external_interner, "external_value");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(loaded_value->view.render_hint, "ref");

    fs::remove_all(dir);
}

TEST(DocumentSafety, SaveLoadDropsSessionOnlyNodeColor) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_save_load_color_session_only");
    const fs::path bp_path = dir / "color.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint seed;
    seed = seed.with_id(interner.intern("color_seed"));
    seed = seed.with_display_name("Color Seed");

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("slider1");
    node.semantic.type = interner.intern("Slider");
    seed = seed.with_node(std::move(node));

    write_file(bp_path, bp2::BlueprintCodec::encode(seed, interner, arena, &registry));
    ASSERT_TRUE(doc.load(bp_path.string()));

    bp2::Blueprint colored = doc.model().current();
    bp2::Blueprint::Node updated = *require_node(colored, doc.interner(), "slider1");
    updated.view.has_color = true;
    updated.view.color_r = 0.8f;
    updated.view.color_g = 0.2f;
    updated.view.color_b = 0.1f;
    updated.view.color_a = 1.0f;
    doc.model().replace_current(bp2::replace_node_preserve_order(colored, std::move(updated)));

    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setTypeRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_node = require_node(loaded.model().current(), loaded.interner(), "slider1");
    ASSERT_NE(loaded_node, nullptr);
    EXPECT_FALSE(loaded_node->view.has_color);

    fs::remove_all(dir);
}
