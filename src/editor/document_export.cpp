#include "document.h"

#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
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

    if (type_registry_) {
        std::string library_path = "library/";
        {
            std::filesystem::path lp(library_path);
            if (!std::filesystem::exists(lp) && lp.is_relative()) {
                std::vector<std::filesystem::path> try_paths = {
                    lp, "../" / lp, "../../" / lp, "../../../" / lp,
                };
                for (const auto& p : try_paths) {
                    if (std::filesystem::exists(p)) {
                        library_path = p.string();
                        break;
                    }
                }
            }
        }

        for (const auto& [classname, def] : type_registry_->types) {
            if (def.cpp_class) {
                continue;
            }

            std::filesystem::path blueprint_file = std::filesystem::path(library_path);
            auto cat_it = type_registry_->categories.find(classname);
            if (cat_it != type_registry_->categories.end() && !cat_it->second.empty()) {
                blueprint_file /= cat_it->second;
            }
            blueprint_file /= (classname + ".blueprint");

            auto loaded = load_blueprint_from_file_validated(
                blueprint_file.string().c_str(),
                interner_,
                arena_,
                *type_registry_);
            if (!loaded) {
                spdlog::warn("[editor] export flatten: failed to load reference blueprint '{}' from '{}'",
                             classname,
                             blueprint_file.string());
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
