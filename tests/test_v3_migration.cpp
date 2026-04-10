#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "json_parser/json_parser.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/path/path.h"
static std::string read_file(std::string const& path) {
    std::ifstream f(path);
    EXPECT_TRUE(f.is_open()) << path;
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

TEST(V3Migration, LibraryLoaderParsesConvertedLibraryAsV3Only) {
    TypeRegistry registry = load_type_registry("library");

    EXPECT_TRUE(registry.has("ElectricalSource"));
    EXPECT_TRUE(registry.has("Generator"));
}

TEST(V3Migration, CodecRejectsLegacyVersion2Json) {
    std::string old_schema = R"({
        "format": "an24.blueprint",
        "version": 2,
        "blueprint_id": "old_schema",
        "name": "old_schema",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(old_schema, interner, arena, registry, &err);
    EXPECT_FALSE(bp.has_value());
    EXPECT_NE(err.message.find("Unsupported blueprint version"), std::string::npos);
}

TEST(V3Migration, GSCIsV3AndDecodes) {
    const std::string gsc_path = "/Users/vladimir/an24_cpp/GSC.blueprint";
    if (!std::filesystem::exists(gsc_path)) {
        GTEST_SKIP() << "GSC.blueprint not present (workspace save file, not source-controlled)";
    }

    std::string content = read_file(gsc_path);

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(content, interner, arena, registry, &err);
    ASSERT_TRUE(bp.has_value()) << err.message;
    EXPECT_FALSE(bp->nodes().empty());
    EXPECT_FALSE(bp->wires().empty());
}
