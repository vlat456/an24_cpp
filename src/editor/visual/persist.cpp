#include "visual/persist.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/registry/type_registry.h"
#include "ui/core/interned_id.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

bool save_blueprint_to_file(const bp2::Blueprint& bp,
                             ui::StringInterner const& interner,
                             bp2::PathArena const& arena,
                             const char* path) {
    namespace fs = std::filesystem;
    fs::path save_path = fs::weakly_canonical(fs::path(path));
    for (auto it = save_path.begin(); it != save_path.end(); ++it) {
        if (*it == "library") {
            spdlog::error("Refusing to save into library/ directory: {}", path);
            return false;
        }
    }
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << json_str;
    return true;
}

std::optional<bp2::Blueprint> load_blueprint_from_file(
        const char* path,
        ui::StringInterner& interner,
        bp2::PathArena& arena) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << file.rdbuf();
    bp2::DecodeError err;
    bp2::TypeRegistry registry;
    auto bp = bp2::BlueprintCodec::decode(buffer.str(), interner, arena, registry, &err);
    if (!bp) {
        spdlog::error("[persist] Failed to load blueprint: {}", err.message);
        return std::nullopt;
    }
    return bp;
}
