#include <gtest/gtest.h>
#include "editor/visual/workspace_session.h"
#include "editor/visual/workspace_session_persist.h"
#include "editor/visual/persist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

TEST(WorkspaceSessionSeparation, BlueprintAndWorkspaceAreSeparate) {
    // This test proves that blueprint documents are completely separate from
    // workspace/session files and have no overlapping fields.

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    // Create a simple blueprint
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("separation_test"));
    bp = bp.with_name("Separation Test");

    fs::path tmp_dir = fs::temp_directory_path() / "ws_blueprint_sep_test";
    fs::create_directories(tmp_dir);

    fs::path bp_path = tmp_dir / "test.blueprint";
    
    // Save blueprint
    ASSERT_TRUE(save_blueprint_to_file(bp, interner, arena, parser_registry, bp_path.c_str()));
    
    // Load and verify blueprint has correct format
    std::ifstream bp_in(bp_path);
    nlohmann::json bp_j;
    bp_in >> bp_j;
    
    EXPECT_EQ(bp_j["format"], "blueprint");
    EXPECT_EQ(bp_j["version"], 1);
    EXPECT_FALSE(bp_j.contains("viewport"));
    EXPECT_FALSE(bp_j.contains("editor"));
    EXPECT_FALSE(bp_j.contains("pan_x"));
    EXPECT_FALSE(bp_j.contains("pan_y"));
    EXPECT_FALSE(bp_j.contains("zoom"));
    EXPECT_FALSE(bp_j.contains("grid_step"));
    EXPECT_FALSE(bp_j.contains("open_windows"));
    EXPECT_FALSE(bp_j.contains("selection"));
    EXPECT_FALSE(bp_j.contains("session"));
    EXPECT_FALSE(bp_j.contains("workspace"));

    // Now save workspace session
    WorkspaceSession ws;
    ws.viewport_pan_x = 50.0f;
    ws.viewport_pan_y = 75.0f;
    ws.viewport_zoom = 2.0f;
    ws.grid_step = 48;
    ws.open_windows.push_back("win_1");

    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));

    // Load and verify workspace has correct format
    fs::path ws_path = tmp_dir / "test.workspace.json";
    ASSERT_TRUE(fs::exists(ws_path));

    std::ifstream ws_in(ws_path);
    nlohmann::json ws_j;
    ws_in >> ws_j;

    EXPECT_EQ(ws_j["format"], "an24.workspace_session");
    EXPECT_EQ(ws_j["version"], 1);
    EXPECT_TRUE(ws_j.contains("viewport"));
    EXPECT_TRUE(ws_j.contains("editor"));
    EXPECT_EQ(ws_j["viewport"]["pan_x"], 50.0f);
    EXPECT_EQ(ws_j["viewport"]["pan_y"], 75.0f);
    EXPECT_EQ(ws_j["viewport"]["zoom"], 2.0f);
    EXPECT_EQ(ws_j["viewport"]["grid_step"], 48);

    // Verify blueprint file was NOT modified by workspace persistence
    std::ifstream bp_in2(bp_path);
    nlohmann::json bp_j2;
    bp_in2 >> bp_j2;

    EXPECT_EQ(bp_j2["format"], "blueprint");
    EXPECT_FALSE(bp_j2.contains("viewport"));
    EXPECT_FALSE(bp_j2.contains("editor"));

    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

TEST(BlueprintPersistenceSpec, NoForbiddenFieldsInSavedBlueprint) {
    // Verify spec compliance: saved blueprints must not have workspace fields
    
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("spec_test"));
    bp = bp.with_name("Spec Test");

    fs::path tmp = fs::temp_directory_path() / "bp2_spec_compliance.blueprint";
    ASSERT_TRUE(save_blueprint_to_file(bp, interner, arena, parser_registry, tmp.c_str()));

    std::ifstream in(tmp);
    nlohmann::json j;
    in >> j;

    // List of forbidden fields from persistence_spec_v1.md
    const std::vector<std::string> forbidden_fields = {
        "display_name",           // v0 field
        "nested",                 // v0 field
        "pan_x", "pan_y",         // viewport state
        "zoom", "grid_step",      // viewport state
        "viewport",               // workspace struct
        "editor",                 // workspace struct
        "open_windows",           // editor state
        "selection",              // editor state
        "session",                // workspace state
        "workspace"               // workspace state
    };

    for (const auto& forbidden : forbidden_fields) {
        EXPECT_FALSE(j.contains(forbidden)) 
            << "Blueprint must not contain forbidden field: " << forbidden;
    }

    std::error_code ec;
    fs::remove(tmp, ec);
}
