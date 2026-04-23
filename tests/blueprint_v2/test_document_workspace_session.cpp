#include <gtest/gtest.h>

#include "editor/document.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/library/library_index.h"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

fs::path make_temp_dir(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open()) << path;
    out << contents;
}

bp2::Blueprint make_root_blueprint(ui::StringInterner& interner) {
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("workspace_doc"));
    bp = bp.with_name("Workspace Doc");

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("host1");
    host.semantic.type = interner.intern("embedded_type");
    host.view.name = "host1";

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("embedded_type"));
    inner = inner.with_name("Embedded Type");

    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(std::move(inner).with_id(interner.intern("embedded_type"))))
    };

    bp = bp.with_node(std::move(host));

    bp2::Blueprint::Node ref;
    ref.semantic.id = interner.intern("ref1");
    ref.semantic.type = interner.intern("FirstOrderLag");
    ref.view.name = "ref1";
    ref.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("FirstOrderLag"),
        bp2::Interface{})
    };
    bp = bp.with_node(std::move(ref));

    return bp;
}

} // namespace

TEST(DocumentWorkspaceSession, SaveAndLoadRoundTripAppliesViewportAndReopensWindows) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);
    bp2::LibraryIndex index;
    index.entries["FirstOrderLag"] = "library/math/FirstOrderLag.blueprint";
    doc.setLibraryIndex(&index);

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp = make_root_blueprint(interner);

    const fs::path dir = make_temp_dir("an24_doc_workspace_session_roundtrip");
    const fs::path bp_path = dir / "doc.blueprint";
    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));
    doc.viewport().pan.x = 111.0f;
    doc.viewport().pan.y = 222.0f;
    doc.viewport().zoom = 1.75f;
    doc.viewport().grid_step = 24.0f;
    doc.openSubWindow(WindowScopeId::embedded({doc.interner().intern("host1")}));
    doc.openSubWindow(WindowScopeId::external({doc.interner().intern("ref1")}));

    ASSERT_TRUE(doc.saveWorkspaceSession());

    Document restored;
    restored.setComponentRegistry(&registry);
    restored.setLibraryIndex(&index);
    ASSERT_TRUE(restored.load(bp_path.string()));
    ASSERT_TRUE(restored.loadWorkspaceSession());

    EXPECT_FLOAT_EQ(restored.viewport().pan.x, 111.0f);
    EXPECT_FLOAT_EQ(restored.viewport().pan.y, 222.0f);
    EXPECT_FLOAT_EQ(restored.viewport().zoom, 1.75f);
    EXPECT_FLOAT_EQ(restored.viewport().grid_step, 24.0f);
    EXPECT_NE(restored.windowManager().find(WindowScopeId::embedded({restored.interner().intern("host1")})), nullptr);
    EXPECT_NE(restored.windowManager().find(WindowScopeId::external({restored.interner().intern("ref1")})), nullptr);

    fs::remove_all(dir);
}
