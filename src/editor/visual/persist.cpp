#include "visual/persist.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "editor/blueprint_view_hydration.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdlib>
#include <vector>
#include <spdlog/spdlog.h>

bool validate_blueprint_for_persist(
    const bp2::Blueprint& bp,
    ui::StringInterner& interner,
    const bp2::PathArena& arena,
    const TypeRegistry& parser_registry,
    std::string* error_out);

bool validate_blueprint_integrity(
        const bp2::Blueprint& bp,
        ui::StringInterner& interner,
        const bp2::PathArena& arena,
        const TypeRegistry& parser_registry,
        std::string* error_out);

bool save_blueprint_to_file(const bp2::Blueprint& bp,
                            ui::StringInterner& interner,
                            bp2::PathArena const& arena,
                            const TypeRegistry& parser_registry,
                            const char* path) {
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena, &parser_registry);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << json_str;
    return true;
}

std::optional<bp2::Blueprint> load_blueprint_from_file(
        const char* path,
        ui::StringInterner& interner,
        bp2::PathArena& arena,
        const TypeRegistry& parser_registry) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << file.rdbuf();
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(buffer.str(), interner, arena, parser_registry, &err);
    if (!bp) {
        spdlog::error("[persist] Failed to load blueprint: {}", err.message);
        return std::nullopt;
    }
    return bp;
}

std::optional<bp2::Blueprint> load_blueprint_from_file_validated(
        const char* path,
        ui::StringInterner& interner,
        bp2::PathArena& arena,
        const TypeRegistry& parser_registry) {
    auto bp = load_blueprint_from_file(path, interner, arena, parser_registry);
    if (!bp) {
        return std::nullopt;
    }

    std::string err;
    if (!validate_blueprint_for_persist(*bp, interner, arena, parser_registry, &err)) {
        spdlog::error("[persist] Invariant validation failed for '{}': {}", path, err);
        return std::nullopt;
    }

    return bp;
}

std::optional<bp2::Blueprint> load_hydrated_blueprint_from_file(
        const char* path,
        ui::StringInterner& interner,
        bp2::PathArena& arena,
        const TypeRegistry& parser_registry) {
    auto bp = load_blueprint_from_file_validated(path, interner, arena, parser_registry);
    if (!bp) {
        return std::nullopt;
    }
    return editor::hydrate_runtime_node_view_data(std::move(*bp), interner, parser_registry);
}

bool validate_blueprint_for_persist(
        const bp2::Blueprint& bp,
        ui::StringInterner& interner,
        const bp2::PathArena& arena,
        const TypeRegistry& parser_registry,
        std::string* error_out) {
    std::string integrity_err;
    if (!validate_blueprint_integrity(bp, interner, arena, parser_registry, &integrity_err)) {
        if (error_out) *error_out = integrity_err;
        return false;
    }

     for (const auto& node : bp.nodes()) {
         // Embedded blueprint instances carry a semantic type that matches
         // the blueprint ID, not a registered component type. Skip validation.
         if (node.has_embedded_blueprint()) {
             continue;
         }
         std::string type_name(interner.resolve(node.semantic.type));
         if (!parser_registry.has(type_name)) {
             if (error_out) *error_out = "unknown node type: " + type_name;
             return false;
         }
     }

    if (error_out) error_out->clear();
    return true;
}

bool validate_blueprint_integrity(
        const bp2::Blueprint& bp,
        ui::StringInterner& interner,
        const bp2::PathArena& arena,
        const TypeRegistry& parser_registry,
        std::string* error_out) {
    auto inv = bp2::InvariantChecker::validate(bp, arena, parser_registry, interner);
    if (!inv.valid) {
        if (error_out) *error_out = inv.error;
        return false;
    }
    if (error_out) error_out->clear();
    return true;
}
