#include <gtest/gtest.h>

#include "blueprint_v2/library/library_index.h"

#include <fstream>
#include <filesystem>
#include <cstdio>

namespace {

/// Write a temporary JSON file and return its path.
/// The file is placed in the system temp directory.
class TempJsonFile {
public:
    explicit TempJsonFile(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() / "test_library_index_XXXXXX.json";
        // Make unique name
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("test_library_index_" + std::to_string(counter++) + ".json");
        std::ofstream ofs(path_);
        ofs << content;
    }

    ~TempJsonFile() {
        std::filesystem::remove(path_);
    }

    const std::string& path() const { return path_string_; }
    std::string str() const { return path_.string(); }

private:
    std::filesystem::path path_;
    std::string path_string_;
};

std::string write_temp(const std::string& content) {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("test_library_index_" + std::to_string(counter++) + ".json");
    std::ofstream ofs(path);
    ofs << content;
    return path.string();
}

void remove_temp(const std::string& path) {
    std::filesystem::remove(path);
}

} // namespace

TEST(LibraryIndex, LoadsValidIndex) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "Battery", "path": "library/electrical/Battery.blueprint"},
            {"blueprint_id": "Resistor", "path": "library/electrical/Resistor.blueprint"}
        ]
    })";
    auto path = write_temp(content);
    auto index = bp2::load_library_index(path);
    remove_temp(path);

    EXPECT_EQ(index.size(), 2u);
    EXPECT_TRUE(index.has("Battery"));
    EXPECT_TRUE(index.has("Resistor"));
    EXPECT_FALSE(index.has("Missing"));

    auto resolved = index.resolve("Battery");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, "library/electrical/Battery.blueprint");

    auto missing = index.resolve("Missing");
    EXPECT_FALSE(missing.has_value());
}

TEST(LibraryIndex, RejectsWrongFormat) {
    std::string content = R"({
        "format": "wrong_format",
        "version": 1,
        "entries": []
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsWrongVersion) {
    std::string content = R"({
        "format": "library_index",
        "version": 99,
        "entries": []
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsUnknownTopLevelField) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [],
        "extra_field": true
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsDuplicateBlueprintId) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "Battery", "path": "library/a/Battery.blueprint"},
            {"blueprint_id": "Battery", "path": "library/b/Battery2.blueprint"}
        ]
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsDuplicatePath) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "Battery", "path": "library/electrical/Battery.blueprint"},
            {"blueprint_id": "Resistor", "path": "library/electrical/Battery.blueprint"}
        ]
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsEmptyBlueprintId) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "", "path": "library/electrical/Battery.blueprint"}
        ]
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsEmptyPath) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "Battery", "path": ""}
        ]
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsUnknownEntryField) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": [
            {"blueprint_id": "Battery", "path": "library/Battery.blueprint", "extra": true}
        ]
    })";
    auto path = write_temp(content);
    EXPECT_THROW(bp2::load_library_index(path), std::runtime_error);
    remove_temp(path);
}

TEST(LibraryIndex, RejectsMissingFile) {
    EXPECT_THROW(bp2::load_library_index("/nonexistent/path.json"), std::runtime_error);
}

TEST(LibraryIndex, LoadsEmptyEntries) {
    std::string content = R"({
        "format": "library_index",
        "version": 1,
        "entries": []
    })";
    auto path = write_temp(content);
    auto index = bp2::load_library_index(path);
    remove_temp(path);

    EXPECT_EQ(index.size(), 0u);
}

TEST(LibraryIndex, LoadsRealLibraryIndex) {
    // Test loading the actual library/library_index.json if it exists
    const char* candidates[] = {
        "library/library_index.json",
        "../library/library_index.json",
        "../../library/library_index.json",
        "../../../library/library_index.json",
    };
    std::string real_path;
    for (const char* c : candidates) {
        if (std::filesystem::exists(c)) {
            real_path = c;
            break;
        }
    }
    if (real_path.empty()) {
        GTEST_SKIP() << "library/library_index.json not found relative to CWD";
    }

    auto index = bp2::load_library_index(real_path);
    EXPECT_GE(index.size(), 70u);  // We have 78 entries
    EXPECT_TRUE(index.has("Resistor"));
    EXPECT_TRUE(index.has("PID"));

    auto resolved = index.resolve("Resistor");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_NE(resolved->find("Resistor"), std::string::npos);
}
