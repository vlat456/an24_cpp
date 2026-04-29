#include <gtest/gtest.h>
#include "editor/window/script_editor_window.h"
#include "editor/commands/commands.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "core/strings/interned_id.h"
#include "input/editing_host.h"

static bp2::Blueprint::Node make_lua_node(core::StringInterner& I,
                                           const char* id,
                                           const std::string& script) {
    bp2::Blueprint::Node n;
    n.semantic.id   = I.intern(id);
    n.semantic.type = I.intern("LuaScript");
    n.view.name = id;
    n.semantic.string_params["script"] = script;
    return n;
}

class ScriptEditorTest : public ::testing::Test {
protected:
    core::StringInterner interner;
    bp2::EditorModel   model;
};

TEST_F(ScriptEditorTest, OpenSetsTargetAndScript) {
    model.add_node(make_lua_node(interner, "lua1",
        "function process(inputs, dt) return {inputs[1]} end"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    EXPECT_FALSE(win.is_open());

    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    EXPECT_TRUE(win.is_open());
    EXPECT_EQ(win.pending_script(),
        "function process(inputs, dt) return {inputs[1]} end");
}

TEST_F(ScriptEditorTest, CloseClearsOpenState) {
    model.add_node(make_lua_node(interner, "lua1", "x = 1"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    EXPECT_TRUE(win.is_open());
    win.close();
    EXPECT_FALSE(win.is_open());
}

TEST_F(ScriptEditorTest, OpenWithNullHostDoesNotOpen) {
    model.add_node(make_lua_node(interner, "lua1", "x = 1"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"), nullptr, interner, [](core::InternedId) {});
    EXPECT_FALSE(win.is_open());
}

TEST_F(ScriptEditorTest, PendingScriptCanBeModifiedBeforeApply) {
    model.add_node(make_lua_node(interner, "lua1", "original"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    EXPECT_EQ(win.pending_script(), "original");
    win.set_pending_script("modified");
    EXPECT_EQ(win.pending_script(), "modified");
}

TEST_F(ScriptEditorTest, ApplyUpdatesBlueprintScript) {
    model.add_node(make_lua_node(interner, "lua1", "original"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    win.set_pending_script("function process(inputs, dt) return {} end");
    win.apply();

    EXPECT_FALSE(win.is_open());

    const bp2::Blueprint::Node* updated = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(updated, nullptr);
    auto it = updated->semantic.string_params.find("script");
    ASSERT_NE(it, updated->semantic.string_params.end());
    EXPECT_EQ(it->second, "function process(inputs, dt) return {} end");
}

TEST_F(ScriptEditorTest, ApplyNoChangesDoesNotCreateCheckpoint) {
    model.add_node(make_lua_node(interner, "lua1", "same"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    size_t initial_depth = model.undo_depth();

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    win.apply();

    EXPECT_EQ(model.undo_depth(), initial_depth);
}

TEST_F(ScriptEditorTest, NodeDeletedWhileOpenClosesGracefully) {
    model.add_node(make_lua_node(interner, "lua1", "x = 1"));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua1"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua1"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    EXPECT_TRUE(win.is_open());

    model.remove_node(interner.intern("lua1"));

    win.render();
    EXPECT_FALSE(win.is_open());
}

TEST_F(ScriptEditorTest, MissingScriptParamDefaultsToEmpty) {
    bp2::Blueprint::Node n;
    n.semantic.id   = interner.intern("lua2");
    n.semantic.type = interner.intern("LuaScript");
    n.view.name = "lua2";
    model.add_node(n);

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lua2"));
    ASSERT_NE(node_ptr, nullptr);

    ScriptEditorWindow win;
    win.open(*node_ptr, interner.intern("lua2"),
        create_editor_model_host(model, nullptr, &interner, nullptr),
        interner, [](core::InternedId) {});

    EXPECT_EQ(win.pending_script(), "");
}
