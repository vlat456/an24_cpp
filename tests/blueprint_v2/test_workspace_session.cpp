#include <gtest/gtest.h>
#include "editor/visual/workspace_session.h"
#include "editor/visual/workspace_session_persist.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

class WorkspaceSessionTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "ws_session_tests";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    fs::path make_blueprint_path(const std::string& name) {
        return temp_dir / (name + ".blueprint");
    }

    fs::path make_workspace_path(const std::string& name) {
        return temp_dir / (name + ".workspace.json");
    }
};

TEST_F(WorkspaceSessionTest, SaveAndLoadBasicSession) {
    WorkspaceSession ws;
    ws.viewport_pan_x = 100.0f;
    ws.viewport_pan_y = 200.0f;
    ws.viewport_zoom = 2.5f;
    ws.grid_step = 64;
    ws.open_windows.push_back("window_1");
    ws.open_windows.push_back("window_2");

    fs::path bp_path = make_blueprint_path("test_basic");
    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));

    // Verify workspace file was created
    fs::path ws_path = make_workspace_path("test_basic");
    ASSERT_TRUE(fs::exists(ws_path));

    // Load and verify
    auto loaded = load_workspace_session(bp_path.c_str());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FLOAT_EQ(loaded->viewport_pan_x, 100.0f);
    EXPECT_FLOAT_EQ(loaded->viewport_pan_y, 200.0f);
    EXPECT_FLOAT_EQ(loaded->viewport_zoom, 2.5f);
    EXPECT_EQ(loaded->grid_step, 64);
    ASSERT_EQ(loaded->open_windows.size(), 2u);
    EXPECT_EQ(loaded->open_windows[0], "window_1");
    EXPECT_EQ(loaded->open_windows[1], "window_2");
}

TEST_F(WorkspaceSessionTest, LoadNonexistentFileReturnsEmpty) {
    fs::path bp_path = make_blueprint_path("nonexistent");
    auto loaded = load_workspace_session(bp_path.c_str());
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(WorkspaceSessionTest, DefaultWorkspaceIsIdentified) {
    WorkspaceSession ws;
    EXPECT_TRUE(ws.isDefault());

    ws.viewport_pan_x = 1.0f;
    EXPECT_FALSE(ws.isDefault());

    WorkspaceSession ws2;
    ws2.viewport_zoom = 0.5f;
    EXPECT_FALSE(ws2.isDefault());

    WorkspaceSession ws3;
    ws3.grid_step = 32;
    EXPECT_FALSE(ws3.isDefault());

    WorkspaceSession ws4;
    ws4.open_windows.push_back("test");
    EXPECT_FALSE(ws4.isDefault());

}

TEST_F(WorkspaceSessionTest, WorkspaceFileHasCorrectFormat) {
    WorkspaceSession ws;
    ws.viewport_pan_x = 50.0f;
    ws.viewport_pan_y = 75.0f;
    ws.viewport_zoom = 1.5f;
    ws.grid_step = 48;
    ws.open_windows.push_back("win_1");

    fs::path bp_path = make_blueprint_path("test_format");
    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));

    fs::path ws_path = make_workspace_path("test_format");
    std::ifstream in(ws_path);
    ASSERT_TRUE(in.is_open());
    nlohmann::json j;
    in >> j;

    // Check format and version
    ASSERT_TRUE(j.contains("format"));
    EXPECT_EQ(j["format"], "an24.workspace_session");
    ASSERT_TRUE(j.contains("version"));
    EXPECT_EQ(j["version"], 1);

    // Check viewport
    ASSERT_TRUE(j.contains("viewport"));
    EXPECT_FLOAT_EQ(j["viewport"]["pan_x"], 50.0f);
    EXPECT_FLOAT_EQ(j["viewport"]["pan_y"], 75.0f);
    EXPECT_FLOAT_EQ(j["viewport"]["zoom"], 1.5f);
    EXPECT_EQ(j["viewport"]["grid_step"], 48);

    // Check editor state
    ASSERT_TRUE(j.contains("editor"));
    ASSERT_TRUE(j["editor"].contains("open_windows"));
}

TEST_F(WorkspaceSessionTest, WorkspaceIndependentOfBlueprint) {
    // This test verifies that workspace persistence does NOT touch blueprint files
    WorkspaceSession ws;
    ws.viewport_pan_x = 123.0f;

    fs::path bp_path = make_blueprint_path("test_independent");
    
    // Create a dummy blueprint file
    {
        std::ofstream bp_out(bp_path);
        bp_out << R"({"format":"blueprint","version":1,"blueprint_id":"test","name":"Test","interface":[],"nodes":[],"wires":[]})";
    }

    // Save workspace
    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));

    // Verify blueprint file was NOT modified by workspace persistence
    std::ifstream bp_in(bp_path);
    nlohmann::json bp_j;
    bp_in >> bp_j;
    
    // Blueprint should have no workspace fields
    EXPECT_FALSE(bp_j.contains("viewport"));
    EXPECT_FALSE(bp_j.contains("editor"));
    EXPECT_FALSE(bp_j.contains("open_windows"));
    EXPECT_FALSE(bp_j.contains("selection"));
    EXPECT_EQ(bp_j["format"], "blueprint");
    EXPECT_NE(bp_j["format"], "an24.workspace_session");
}

TEST_F(WorkspaceSessionTest, EmptyWorkspaceCanBeSaved) {
    WorkspaceSession ws;  // Default/empty
    fs::path bp_path = make_blueprint_path("test_empty");
    
    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));
    
    fs::path ws_path = make_workspace_path("test_empty");
    ASSERT_TRUE(fs::exists(ws_path));

    auto loaded = load_workspace_session(bp_path.c_str());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->isDefault());
}

TEST_F(WorkspaceSessionTest, InvalidWorkspaceFileReturnsEmpty) {
    fs::path ws_path = make_workspace_path("test_invalid");
    
    // Create invalid JSON
    {
        std::ofstream out(ws_path);
        out << "{ invalid json }";
    }

    fs::path bp_path = make_blueprint_path("test_invalid");
    auto loaded = load_workspace_session(bp_path.c_str());
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(WorkspaceSessionTest, InvalidFormatReturnsEmpty) {
    fs::path ws_path = make_workspace_path("test_wrong_format");
    
    // Create valid JSON but wrong format
    {
        std::ofstream out(ws_path);
        out << R"({"format":"wrong.format","version":1,"viewport":{"pan_x":0}})";
    }

    fs::path bp_path = make_blueprint_path("test_wrong_format");
    auto loaded = load_workspace_session(bp_path.c_str());
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(WorkspaceSessionTest, WorkspaceFileNameDerivation) {
    // Test that .blueprint extension is handled correctly
    WorkspaceSession ws;
    ws.viewport_zoom = 2.0f;

    // Test with .blueprint extension
    fs::path bp_path = make_blueprint_path("test_derivation");
    ASSERT_TRUE(save_workspace_session(ws, bp_path.c_str()));
    fs::path expected_ws = make_workspace_path("test_derivation");
    ASSERT_TRUE(fs::exists(expected_ws));

    // Test loading with the same path
    auto loaded = load_workspace_session(bp_path.c_str());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FLOAT_EQ(loaded->viewport_zoom, 2.0f);
}
