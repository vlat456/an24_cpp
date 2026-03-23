#include "visual/persist.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/registry/type_registry.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {

bp2::Direction to_bp2_direction(PortDirection dir) {
    switch (dir) {
        case PortDirection::In: return bp2::Direction::Input;
        case PortDirection::Out: return bp2::Direction::Output;
        case PortDirection::InOut: return bp2::Direction::InOut;
    }
    return bp2::Direction::Output;
}

bp2::TypeRegistry build_bp2_registry(ui::StringInterner& interner) {
    bp2::TypeRegistry out;
    TypeRegistry parsed = load_type_registry("library/");

    for (const auto& [classname, def] : parsed.types) {
        std::vector<bp2::PortDescriptor> ports;
        ports.reserve(def.ports.size());

        for (const auto& [name, port] : def.ports) {
            bp2::PortDescriptor pd;
            pd.name = interner.intern(name);
            pd.domain = static_cast<Domain>(port.type);
            pd.direction = to_bp2_direction(port.direction);
            ports.push_back(pd);
        }

        bp2::Interface iface(std::move(ports));
        ui::InternedId type_id = interner.intern(classname);
        if (def.cpp_class) {
            out.register_component(type_id, iface, def.description);
        } else {
            out.register_blueprint(type_id, iface, def.description, nullptr);
        }

        if (auto* entry = const_cast<bp2::TypeRegistry::Entry*>(out.find(type_id))) {
            entry->param_defaults = def.params;
        }
    }

    return out;
}

} // namespace

bool validate_blueprint_for_persist(
    const bp2::Blueprint& bp,
    ui::StringInterner& interner,
    const bp2::PathArena& arena,
    const TypeRegistry& parser_registry,
    std::string* error_out);

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
    bp2::TypeRegistry registry = build_bp2_registry(interner);
    auto bp = bp2::BlueprintCodec::decode(buffer.str(), interner, arena, registry, &err);
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
    auto bp = load_blueprint_from_file(path, interner, arena);
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

bool validate_blueprint_for_persist(
        const bp2::Blueprint& bp,
        ui::StringInterner& interner,
        const bp2::PathArena& arena,
        const TypeRegistry& parser_registry,
        std::string* error_out) {
    bp2::TypeRegistry bp2_registry = build_bp2_registry(interner);
    auto inv = bp2::InvariantChecker::validate(bp, arena, bp2_registry);
    if (!inv.valid) {
        if (error_out) *error_out = inv.error;
        return false;
    }

    for (const auto& node : bp.nodes()) {
        std::string type_name(interner.resolve(node.type));
        if (!parser_registry.has(type_name)) {
            if (error_out) *error_out = "unknown node type: " + type_name;
            return false;
        }
    }

    if (error_out) error_out->clear();
    return true;
}
