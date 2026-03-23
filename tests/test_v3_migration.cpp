#include <gtest/gtest.h>

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

    EXPECT_TRUE(registry.has("Battery"));
    EXPECT_TRUE(registry.has("RUG82"));
    EXPECT_TRUE(registry.has("RUG_82_1"));

    auto const* rug = registry.get("RUG_82_1");
    ASSERT_NE(rug, nullptr);
    EXPECT_FALSE(rug->cpp_class);
    EXPECT_FALSE(rug->devices.empty());
    EXPECT_FALSE(rug->connections.empty());
}

TEST(V3Migration, CodecRejectsLegacyVersion2Json) {
    std::string old_schema = R"({
        "version": 2,
        "meta": {"name": "old_schema"},
        "nodes": {},
        "wires": []
    })";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry registry;
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(old_schema, interner, arena, registry, &err);
    EXPECT_FALSE(bp.has_value());
    EXPECT_NE(err.message.find("Unsupported blueprint version"), std::string::npos);
}

TEST(V3Migration, GSCIsV3AndDecodes) {
    std::string content = read_file("/Users/vladimir/an24_cpp/GSC.blueprint");

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry registry;
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(content, interner, arena, registry, &err);
    ASSERT_TRUE(bp.has_value()) << err.message;
    EXPECT_FALSE(bp->nodes().empty());
    EXPECT_FALSE(bp->wires().empty());
}
