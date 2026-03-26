#include <gtest/gtest.h>

#include "editor/editor_settings.h"

#include <filesystem>
#include <fstream>

namespace {

static std::string make_temp_settings_path(const char* name = "settings_test.json") {
    const auto dir = std::filesystem::temp_directory_path() / "an24_editor_settings_tests";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / name).string();
}

} // namespace

TEST(EditorSettings, LoadFromSkipsNonStringArrayEntries) {
    const std::string path = make_temp_settings_path("settings_invalid_array.json");

    std::ofstream f(path);
    ASSERT_TRUE(f.is_open());
    f << R"({
  "recentFiles": ["/tmp/definitely_not_exists_1.blueprint", 123, true, null],
  "openTabs": [42, "/tmp/definitely_not_exists_2.blueprint", {"x":1}],
  "activeTab": "/tmp/some_active.blueprint"
})";
    f.close();

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));

    EXPECT_TRUE(s.recentFiles().empty());
    EXPECT_TRUE(s.openTabs().empty());
    EXPECT_EQ(s.activeTab(), "/tmp/some_active.blueprint");
}

// ==================================================================
// Regression: addOpenTab during iteration of openTabs() must not crash.
// Simulates the pattern from editor_app.cpp tab restoration where
// iterating openTabs() while calling addOpenTab() would invalidate
// iterators and crash in string copy constructor.
// ==================================================================

TEST(EditorSettings, AddOpenTabDuringSnapshotIteration) {
    // Simulate the fixed pattern: snapshot openTabs() before iterating
    EditorSettings s;
    // Pre-populate enough tabs to trigger reallocation on push_back
    for (int i = 0; i < 8; ++i) {
        s.addOpenTab("/tab_" + std::to_string(i));
    }

    // This is what the FIXED code does: snapshot first
    const auto snapshot = s.openTabs();  // copy
    for (const auto& tab : snapshot) {
        // Simulating what openDocument does: addOpenTab on a new path
        s.addOpenTab(tab + "_reopened");
    }

    // Original 8 tabs + 8 new ones
    EXPECT_EQ(s.openTabs().size(), 16u);
}

TEST(EditorSettings, AddOpenTabDuplicateIsNoop) {
    EditorSettings s;
    s.addOpenTab("/tab_a");
    s.addOpenTab("/tab_b");
    s.addOpenTab("/tab_a");  // duplicate — should be ignored

    EXPECT_EQ(s.openTabs().size(), 2u);
    EXPECT_EQ(s.openTabs()[0], "/tab_a");
    EXPECT_EQ(s.openTabs()[1], "/tab_b");
}

TEST(EditorSettings, AddRecentFileMovesToFront) {
    EditorSettings s;
    s.addRecentFile("/file_a");
    s.addRecentFile("/file_b");
    s.addRecentFile("/file_a");  // should move to front

    ASSERT_EQ(s.recentFiles().size(), 2u);
    EXPECT_EQ(s.recentFiles()[0], "/file_a");
    EXPECT_EQ(s.recentFiles()[1], "/file_b");
}

TEST(EditorSettings, RecentFilesRespectsMaxLimit) {
    EditorSettings s;
    for (size_t i = 0; i < EditorSettings::MAX_RECENT + 5; ++i) {
        s.addRecentFile("/file_" + std::to_string(i));
    }
    EXPECT_EQ(s.recentFiles().size(), EditorSettings::MAX_RECENT);
    // Most recently added should be at front
    EXPECT_EQ(s.recentFiles()[0], "/file_14");
}

TEST(EditorSettings, RoundTripSaveLoad) {
    const std::string path = make_temp_settings_path("settings_roundtrip.json");

    // Create temp files so loadFrom doesn't filter them out via exists() check
    const std::string tab1 = make_temp_settings_path("tab1.blueprint");
    const std::string tab2 = make_temp_settings_path("tab2.blueprint");
    { std::ofstream(tab1) << "{}"; }
    { std::ofstream(tab2) << "{}"; }

    {
        EditorSettings s;
        s.addOpenTab(tab1);
        s.addOpenTab(tab2);
        s.addRecentFile(tab2);
        s.addRecentFile(tab1);
        s.setActiveTab(tab1);
        s.saveTo(path);
    }

    {
        EditorSettings s;
        s.loadFrom(path);
        ASSERT_EQ(s.openTabs().size(), 2u);
        EXPECT_EQ(s.openTabs()[0], tab1);
        EXPECT_EQ(s.openTabs()[1], tab2);
        ASSERT_EQ(s.recentFiles().size(), 2u);
        EXPECT_EQ(s.recentFiles()[0], tab1);
        EXPECT_EQ(s.recentFiles()[1], tab2);
        EXPECT_EQ(s.activeTab(), tab1);
    }

    // Cleanup
    std::filesystem::remove(tab1);
    std::filesystem::remove(tab2);
    std::filesystem::remove(path);
}

TEST(EditorSettings, LoadFromMissingFile) {
    EditorSettings s;
    s.addOpenTab("/existing");
    s.loadFrom("/nonexistent/path/settings.json");
    // loadFrom clears first, so even existing data is gone
    EXPECT_TRUE(s.openTabs().empty());
}

TEST(EditorSettings, LoadFromCorruptedJson) {
    const std::string path = make_temp_settings_path("settings_corrupt.json");
    { std::ofstream(path) << "{ this is not valid json !!!"; }

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    EXPECT_TRUE(s.openTabs().empty());
    EXPECT_TRUE(s.recentFiles().empty());

    std::filesystem::remove(path);
}

TEST(EditorSettings, LoadFromEmptyFile) {
    const std::string path = make_temp_settings_path("settings_empty.json");
    { std::ofstream(path) << ""; }

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    EXPECT_TRUE(s.openTabs().empty());

    std::filesystem::remove(path);
}

TEST(EditorSettings, LoadFromEmptyObject) {
    const std::string path = make_temp_settings_path("settings_empty_obj.json");
    { std::ofstream(path) << "{}"; }

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    EXPECT_TRUE(s.openTabs().empty());
    EXPECT_TRUE(s.recentFiles().empty());
    EXPECT_TRUE(s.activeTab().empty());

    std::filesystem::remove(path);
}

// ==================================================================
// Regression: BUG #1 — loadFrom must not crash when the JSON contains
// paths with embedded nulls, invalid chars, or extremely long strings
// that can cause std::filesystem::exists() to throw filesystem_error.
// ==================================================================

TEST(EditorSettings, LoadFromDoesNotCrashOnMalformedPaths) {
    const std::string path = make_temp_settings_path("settings_bad_paths.json");

    // Paths containing extremely long strings and platform-specific special names
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open());
    f << R"({"recentFiles": [")" << std::string(8192, 'A') << R"("],)"
      << R"("openTabs": ["CON", "NUL", "/dev/null/../../../etc/shadow"],)"
      << R"("activeTab": "valid_but_nonexistent"})";
    f.close();

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    // Should not crash — some paths may or may not exist but no exception propagates
    // activeTab is loaded regardless of existence
    EXPECT_EQ(s.activeTab(), "valid_but_nonexistent");

    std::filesystem::remove(path);
}

// ==================================================================
// Regression: BUG #1 variant — loadFrom with binary garbage file
// must not crash (simulates corrupted settings on disk).
// ==================================================================

TEST(EditorSettings, LoadFromBinaryGarbageFile) {
    const std::string path = make_temp_settings_path("settings_binary.json");

    std::ofstream f(path, std::ios::binary);
    ASSERT_TRUE(f.is_open());
    // Write random binary garbage
    char garbage[] = {'\x00', '\xff', '\xfe', '\x80', '\x01', '{', '"', '\x00'};
    f.write(garbage, sizeof(garbage));
    f.close();

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    EXPECT_TRUE(s.openTabs().empty());
    EXPECT_TRUE(s.recentFiles().empty());

    std::filesystem::remove(path);
}

// ==================================================================
// Regression: BUG #4 — activeTab must be clearable
// (editor clears it when active document has no filepath)
// ==================================================================

TEST(EditorSettings, ActiveTabClearable) {
    EditorSettings s;
    s.setActiveTab("/some/path.blueprint");
    EXPECT_EQ(s.activeTab(), "/some/path.blueprint");

    s.setActiveTab("");
    EXPECT_TRUE(s.activeTab().empty());
}

// ==================================================================
// Regression: BUG #6 — removeOpenTab correctly removes a failed path
// ==================================================================

TEST(EditorSettings, RemoveOpenTabCleansFailedPath) {
    EditorSettings s;
    s.addOpenTab("/good_tab");
    s.addOpenTab("/bad_tab");
    s.addOpenTab("/also_good");

    s.removeOpenTab("/bad_tab");

    ASSERT_EQ(s.openTabs().size(), 2u);
    EXPECT_EQ(s.openTabs()[0], "/good_tab");
    EXPECT_EQ(s.openTabs()[1], "/also_good");
}

// ==================================================================
// Regression: loadFrom with JSON containing only wrong-typed top-level
// keys (e.g. recentFiles is a string, openTabs is a number) must not crash.
// ==================================================================

TEST(EditorSettings, LoadFromWrongTypedTopLevelKeys) {
    const std::string path = make_temp_settings_path("settings_wrong_types.json");

    std::ofstream f(path);
    ASSERT_TRUE(f.is_open());
    f << R"({
  "recentFiles": "not_an_array",
  "openTabs": 42,
  "activeTab": 12345
})";
    f.close();

    EditorSettings s;
    ASSERT_NO_THROW(s.loadFrom(path));
    EXPECT_TRUE(s.openTabs().empty());
    EXPECT_TRUE(s.recentFiles().empty());
    EXPECT_TRUE(s.activeTab().empty());  // not a string, so not loaded

    std::filesystem::remove(path);
}
