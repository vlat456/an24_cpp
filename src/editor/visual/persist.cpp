#include "visual/persist.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/registry/type_registry.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cerrno>
#include <cstdlib>
#include <unordered_map>
#include <vector>
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

Domain to_bp2_domain_from_port_type(PortType t, Domain fallback) {
    switch (t) {
        case PortType::V:
        case PortType::I:
        case PortType::Any:
            return Domain::Electrical;
        case PortType::Bool:
            return Domain::Logical;
        case PortType::RPM:
        case PortType::Position:
            return Domain::Mechanical;
        case PortType::Pressure:
            return Domain::Hydraulic;
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return fallback;
}

bp2::TypeRegistry build_bp2_registry(ui::StringInterner& interner) {
    bp2::TypeRegistry out;
    TypeRegistry parsed = load_type_registry("library/");

    for (const auto& [classname, def] : parsed.types) {
        std::vector<bp2::PortDescriptor> ports;
        ports.reserve(def.ports.size());

        Domain inferred_domain = Domain::Electrical;
        if (def.domains.has_value() && !def.domains->empty()) {
            inferred_domain = (*def.domains)[0];
        }

        for (const auto& [name, port] : def.ports) {
            bp2::PortDescriptor pd;
            pd.name = interner.intern(name);
            pd.domain = to_bp2_domain_from_port_type(port.type, inferred_domain);
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

bool validate_blueprint_integrity(
    const bp2::Blueprint& bp,
    ui::StringInterner& interner,
    const bp2::PathArena& arena,
    std::string* error_out);

bool save_blueprint_to_file(const bp2::Blueprint& bp,
                              ui::StringInterner& interner,
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
    bp2::TypeRegistry registry = build_bp2_registry(interner);
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena, &registry);
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
    std::string integrity_err;
    if (!validate_blueprint_integrity(bp, interner, arena, &integrity_err)) {
        if (error_out) *error_out = integrity_err;
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

bool validate_blueprint_integrity(
        const bp2::Blueprint& bp,
        ui::StringInterner& interner,
        const bp2::PathArena& arena,
        std::string* error_out) {
    bp2::TypeRegistry bp2_registry = build_bp2_registry(interner);

    // Debug/validation should still work on ad-hoc editor node types used in tests or
    // transient documents. Register unknown types with empty interfaces so structural
    // invariants (IDs, paths, wire endpoints) remain checkable.
    for (const auto& node : bp.nodes()) {
        if (!bp2_registry.has(node.type)) {
            std::unordered_map<ui::InternedId, bp2::Direction> dirs;
            for (const auto& p : node.inputs) {
                dirs[p.name] = bp2::Direction::Input;
            }
            for (const auto& p : node.outputs) {
                auto it = dirs.find(p.name);
                if (it == dirs.end()) {
                    dirs[p.name] = bp2::Direction::Output;
                } else if (it->second != bp2::Direction::Output) {
                    it->second = bp2::Direction::InOut;
                }
            }

            std::vector<bp2::PortDescriptor> ports;
            ports.reserve(dirs.size());
            for (const auto& [name, dir] : dirs) {
                ports.push_back({name, Domain::Electrical, dir});
            }

            bp2_registry.register_component(
                node.type,
                bp2::Interface(std::move(ports)),
                "ad-hoc type");
        }
    }
    for (const auto& nested : bp.nested()) {
        if (!nested.embedded && !nested.blueprint_id.empty() && !bp2_registry.has(nested.blueprint_id)) {
            bp2_registry.register_blueprint(
                nested.blueprint_id,
                bp2::Interface(std::vector<bp2::PortDescriptor>{}),
                "ad-hoc nested blueprint",
                nullptr);
        }
    }

    auto inv = bp2::InvariantChecker::validate(bp, arena, bp2_registry);
    if (!inv.valid) {
        if (error_out) *error_out = inv.error;
        return false;
    }
    if (error_out) error_out->clear();
    return true;
}
