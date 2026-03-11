#include <gtest/gtest.h>
#include "editor/recent_files.h"
#include <fstream>
#include <filesystem>

class RecentFilesTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir;
    std::string config_path;
    std::string file1;
    std::string file2;
    std::string file3;

    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "an24_recent_test";
        std::filesystem::create_directories(temp_dir);
        
        config_path = (temp_dir / "recent.cfg").string();
        file1 = (temp_dir / "test1.blueprint").string();
        file2 = (temp_dir / "test2.blueprint").string();
        file3 = (temp_dir / "test3.blueprint").string();
        
        std::ofstream f1(file1);
        std::ofstream f2(file2);
        std::ofstream f3(file3);
        f1.close();
        f2.close();
        f3.close();
        
        std::filesystem::remove(config_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }
};

TEST_F(RecentFilesTest, EmptyByDefault) {
    RecentFiles rf;
    EXPECT_TRUE(rf.empty());
    EXPECT_EQ(rf.files().size(), 0u);
}

TEST_F(RecentFilesTest, AddSingleFile) {
    RecentFiles rf;
    rf.add("/path/to/file.blueprint");
    EXPECT_FALSE(rf.empty());
    EXPECT_EQ(rf.files().size(), 1u);
    EXPECT_EQ(rf.files()[0], "/path/to/file.blueprint");
}

TEST_F(RecentFilesTest, AddMovesToMostRecent) {
    RecentFiles rf;
    rf.add("/a.blueprint");
    rf.add("/b.blueprint");
    rf.add("/c.blueprint");
    
    EXPECT_EQ(rf.files().size(), 3u);
    EXPECT_EQ(rf.files()[0], "/c.blueprint");
    EXPECT_EQ(rf.files()[1], "/b.blueprint");
    EXPECT_EQ(rf.files()[2], "/a.blueprint");
}

TEST_F(RecentFilesTest, ReAddMovesToFront) {
    RecentFiles rf;
    rf.add("/a.blueprint");
    rf.add("/b.blueprint");
    rf.add("/c.blueprint");
    rf.add("/a.blueprint");
    
    EXPECT_EQ(rf.files().size(), 3u);
    EXPECT_EQ(rf.files()[0], "/a.blueprint");
    EXPECT_EQ(rf.files()[1], "/c.blueprint");
    EXPECT_EQ(rf.files()[2], "/b.blueprint");
}

TEST_F(RecentFilesTest, MaxLimitEnforced) {
    RecentFiles rf;
    for (size_t i = 0; i < RecentFiles::MAX + 5; i++) {
        rf.add("/file" + std::to_string(i) + ".blueprint");
    }
    
    EXPECT_EQ(rf.files().size(), RecentFiles::MAX);
    EXPECT_EQ(rf.files()[0], "/file14.blueprint");
    EXPECT_EQ(rf.files().back(), "/file5.blueprint");
}

TEST_F(RecentFilesTest, ClearList) {
    RecentFiles rf;
    rf.add("/a.blueprint");
    rf.add("/b.blueprint");
    rf.clear();
    
    EXPECT_TRUE(rf.empty());
    EXPECT_EQ(rf.files().size(), 0u);
}

TEST_F(RecentFilesTest, RemoveFromMiddle) {
    RecentFiles rf;
    rf.add("/a.blueprint");
    rf.add("/b.blueprint");
    rf.add("/c.blueprint");
    rf.remove("/b.blueprint");
    
    EXPECT_EQ(rf.files().size(), 2u);
    EXPECT_EQ(rf.files()[0], "/c.blueprint");
    EXPECT_EQ(rf.files()[1], "/a.blueprint");
}

TEST_F(RecentFilesTest, RemoveNonExistentDoesNothing) {
    RecentFiles rf;
    rf.add("/a.blueprint");
    rf.remove("/nonexistent.blueprint");
    
    EXPECT_EQ(rf.files().size(), 1u);
}

TEST_F(RecentFilesTest, RemoveFromEmptyDoesNothing) {
    RecentFiles rf;
    rf.remove("/nonexistent.blueprint");
    EXPECT_TRUE(rf.empty());
}

TEST_F(RecentFilesTest, SaveAndLoadRoundtrip) {
    RecentFiles rf1;
    rf1.add(file1);
    rf1.add(file2);
    rf1.add(file3);
    rf1.saveTo(config_path);
    
    RecentFiles rf2;
    rf2.loadFrom(config_path);
    
    EXPECT_EQ(rf2.files().size(), 3u);
    EXPECT_EQ(rf2.files()[0], file3);
    EXPECT_EQ(rf2.files()[1], file2);
    EXPECT_EQ(rf2.files()[2], file1);
}

TEST_F(RecentFilesTest, LoadNonExistentConfigReturnsEmpty) {
    RecentFiles rf;
    rf.loadFrom("/nonexistent/config/path");
    EXPECT_TRUE(rf.empty());
}

TEST_F(RecentFilesTest, LoadFiltersNonExistentFiles) {
    {
        std::ofstream cfg(config_path);
        cfg << file1 << "\n";
        cfg << "/nonexistent/file.blueprint\n";
        cfg << file2 << "\n";
    }
    
    RecentFiles rf;
    rf.loadFrom(config_path);
    
    EXPECT_EQ(rf.files().size(), 2u);
    EXPECT_EQ(rf.files()[0], file1);
    EXPECT_EQ(rf.files()[1], file2);
}

TEST_F(RecentFilesTest, SaveEmptyListCreatesEmptyFile) {
    RecentFiles rf;
    rf.saveTo(config_path);
    
    EXPECT_TRUE(std::filesystem::exists(config_path));
    
    std::ifstream f(config_path);
    std::string line;
    int count = 0;
    while (std::getline(f, line)) count++;
    EXPECT_EQ(count, 0);
}

TEST_F(RecentFilesTest, DuplicateAddDoesNotCreateDuplicates) {
    RecentFiles rf;
    rf.add(file1);
    rf.add(file1);
    rf.add(file1);
    
    EXPECT_EQ(rf.files().size(), 1u);
    EXPECT_EQ(rf.files()[0], file1);
}

TEST_F(RecentFilesTest, OrderPreservedAfterMultipleOperations) {
    RecentFiles rf;
    rf.add(file1);
    rf.add(file2);
    rf.add(file3);
    rf.remove(file2);
    rf.add(file1);
    rf.add(file2);
    
    EXPECT_EQ(rf.files().size(), 3u);
    EXPECT_EQ(rf.files()[0], file2);
    EXPECT_EQ(rf.files()[1], file1);
    EXPECT_EQ(rf.files()[2], file3);
}

// ============================================================================
// Regression tests
// ============================================================================

// REGRESSION: Save must persist data that load can recover
TEST_F(RecentFilesTest, Regression_SaveLoadPersistence) {
    RecentFiles rf1;
    rf1.add(file1);
    rf1.add(file2);
    rf1.add(file3);
    rf1.saveTo(config_path);
    
    // Simulate app restart - new RecentFiles instance
    RecentFiles rf2;
    EXPECT_TRUE(rf2.empty());  // Empty before load
    
    rf2.loadFrom(config_path);
    
    EXPECT_EQ(rf2.files().size(), 3u);
    EXPECT_EQ(rf2.files()[0], file3);  // Most recent first
    EXPECT_EQ(rf2.files()[1], file2);
    EXPECT_EQ(rf2.files()[2], file1);
}

// REGRESSION: Load should not crash on malformed config file
TEST_F(RecentFilesTest, Regression_MalformedConfigDoesNotCrash) {
    {
        std::ofstream cfg(config_path);
        cfg << "not a valid path\n";
        cfg << "\n";  // empty line
        cfg << file1 << "\n";  // valid
        cfg << "another bad path\n";
    }
    
    RecentFiles rf;
    EXPECT_NO_THROW(rf.loadFrom(config_path));
    EXPECT_EQ(rf.files().size(), 1u);  // Only valid file loaded
    EXPECT_EQ(rf.files()[0], file1);
}

// REGRESSION: Re-adding existing file should move it to front (MRU behavior)
TEST_F(RecentFilesTest, Regression_ReAddMovesToFront) {
    RecentFiles rf;
    rf.add(file1);
    rf.add(file2);
    rf.add(file3);
    
    // file1 is at position 2 (oldest)
    EXPECT_EQ(rf.files()[2], file1);
    
    rf.add(file1);  // Re-add should move to front
    
    EXPECT_EQ(rf.files()[0], file1);  // Now at front
    EXPECT_EQ(rf.files().size(), 3u);  // No duplicate
}

// REGRESSION: Paths with spaces should work
TEST_F(RecentFilesTest, Regression_PathsWithSpaces) {
    std::string spaced_path = (temp_dir / "file with spaces.blueprint").string();
    {
        std::ofstream f(spaced_path);
    }
    
    RecentFiles rf;
    rf.add(spaced_path);
    rf.saveTo(config_path);
    
    RecentFiles rf2;
    rf2.loadFrom(config_path);
    
    EXPECT_EQ(rf2.files().size(), 1u);
    EXPECT_EQ(rf2.files()[0], spaced_path);
}
