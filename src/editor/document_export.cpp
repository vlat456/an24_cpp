#include "document.h"

#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/library_index.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <vector>

using json = nlohmann::json;

namespace {

/// Build a BlueprintLibrary from the TypeRegistry (composite blueprints).
/// Shared between build_simulation_json() and build_jit_input().
bp2::BlueprintLibrary build_library(
    const bp2::LibraryIndex* library_index,
    const TypeRegistry* type_registry,
    ui::StringInterner& interner) {

    bp2::BlueprintLibrary library;
    if (library_index && type_registry) {
        for (const auto& [classname, def] : type_registry->types) {
            if (def.cpp_class) continue;
            bp2::Blueprint loaded;
            try {
                loaded = bp2::blueprint_from_type_definition(def, interner, *type_registry);
            } catch (const std::exception& e) {
                spdlog::warn("[editor] export flatten: failed to build blueprint '{}' from TypeDefinition: {}",
                             classname, e.what());
                continue;
            }
            library.add(interner.intern(classname), std::move(loaded));
        }
    }
    return library;
}

} // namespace

std::string Document::build_simulation_json() {
    const bp2::Blueprint& bp = model_.current();
    json out = json::object();
    out["templates"] = json::object();

    bp2::BlueprintLibrary library = build_library(library_index_, type_registry_, interner_);

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena_);
    auto exported = bp2::elaboration::to_simulation_export(netlist, arena_, interner_, type_registry_);
    out["devices"] = std::move(exported.devices);
    out["connections"] = std::move(exported.connections);
    return out.dump(2);
}

JitBuildInput Document::build_jit_input() {
    const bp2::Blueprint& bp = model_.current();

    bp2::BlueprintLibrary library = build_library(library_index_, type_registry_, interner_);

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena_);
    return bp2::elaboration::elaborate_for_jit(netlist, arena_, interner_, type_registry_);
}

std::pair<ui::InternedId, ui::InternedId>
Document::bp2_path_to_node_port(const bp2::Path& path) const {
    if (path.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port_name = path.segment();
    bp2::Path parent = arena_.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    ui::InternedId node_id = parent.segment();
    return {node_id, port_name};
}

std::pair<ui::InternedId, ui::InternedId>
Document::bp2_path_to_node_port(const bp2::WireEndpoint& ep) const {
    return {ep.node, ep.port};
}
