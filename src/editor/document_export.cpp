#include "document.h"

#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/library_index.h"
#include "editor/visual/persist.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <vector>

using json = nlohmann::json;

std::string Document::build_simulation_json() {
    const bp2::Blueprint& bp = model_.current();
    json out = json::object();
    out["templates"] = json::object();

    bp2::BlueprintLibrary library;

    if (library_index_ && type_registry_) {
        for (const auto& [classname, def] : type_registry_->types) {
            if (def.cpp_class) {
                continue;
            }

            auto resolved = library_index_->resolve(classname);
            if (!resolved) {
                spdlog::warn("[editor] export flatten: blueprint '{}' not found in library index",
                             classname);
                continue;
            }

            auto loaded = load_blueprint_from_file_validated(
                resolved->c_str(),
                interner_,
                arena_,
                *type_registry_);
            if (!loaded) {
                spdlog::warn("[editor] export flatten: failed to load reference blueprint '{}' from '{}'",
                             classname,
                             *resolved);
                continue;
            }
            library.add(interner_.intern(classname), std::move(*loaded));
        }
    }

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena_);
    auto exported = bp2::elaboration::to_simulation_export(netlist, arena_, interner_, type_registry_);
    out["devices"] = std::move(exported.devices);
    out["connections"] = std::move(exported.connections);
    return out.dump(2);
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
