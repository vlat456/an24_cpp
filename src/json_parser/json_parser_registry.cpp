#include "json_parser.h"
#include "json_parser_internal_utils.h"

#include "../parse_number.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <string>

using json = nlohmann::json;

namespace {

bp2::Direction bridge_side_from_string(const std::string& side) {
    if (side == "input") return bp2::Direction::Input;
    if (side == "output") return bp2::Direction::Output;
    throw std::runtime_error("Invalid bridge side '" + side + "'");
}

std::pair<ComponentSpec, TypePresentation> parse_blueprint_type_definition(const json& j, const std::filesystem::path& path) {
    TypePresentation pres;

    // Parse classname
    std::string classname;
    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty()) {
        throw std::runtime_error("Missing required non-empty 'id' in '" + path.string() + "'");
    }
    classname = j["id"].get<std::string>();

    // Parse description/display_name
    if (j.contains("display_name") && j["display_name"].is_string()) {
        pres.description = j["display_name"].get<std::string>();
    } else if (j.contains("description") && j["description"].is_string()) {
        pres.description = j["description"].get<std::string>();
    }

    // Parse cpp_class
    bool is_cpp = false;
    if (!j.contains("cpp_class") || !j["cpp_class"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'cpp_class' for component '" + classname + "'");
    }
    is_cpp = j["cpp_class"].get<bool>();

    // Parse priority
    std::string priority = "med";
    if (j.contains("priority") && j["priority"].is_string()) {
        priority = j["priority"].get<std::string>();
    }

    // Parse critical
    bool critical = false;
    if (j.contains("critical") && j["critical"].is_boolean()) {
        critical = j["critical"].get<bool>();
    }

    // Parse content_type and render_hint
    if (j.contains("content_type") && j["content_type"].is_string()) {
        pres.content_type = j["content_type"].get<std::string>();
    }
    if (j.contains("render_hint") && j["render_hint"].is_string()) {
        pres.render_hint = j["render_hint"].get<std::string>();
    }

    // Parse visual_only
    bool visual_only = false;
    if (j.contains("visual_only") && j["visual_only"].is_boolean()) {
        visual_only = j["visual_only"].get<bool>();
    }

    // Parse scheduler_source
    bool scheduler_source = false;
    if (!j.contains("scheduler_source") || !j["scheduler_source"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'scheduler_source' for component '" + classname + "'");
    }
    scheduler_source = j["scheduler_source"].get<bool>();

    // Parse solver_owned_electrical
    bool solver_owned_electrical = false;
    if (is_cpp) {
        if (!j.contains("solver_owned_electrical") || !j["solver_owned_electrical"].is_boolean()) {
            throw std::runtime_error("Missing required boolean 'solver_owned_electrical' for component '" + classname + "'");
        }
        solver_owned_electrical = j["solver_owned_electrical"].get<bool>();
    } else if (j.contains("solver_owned_electrical")) {
        if (!j["solver_owned_electrical"].is_boolean()) {
            throw std::runtime_error("'solver_owned_electrical' must be boolean for component '" + classname + "'");
        }
        solver_owned_electrical = j["solver_owned_electrical"].get<bool>();
    }

    // Parse domains
    std::vector<Domain> domains;
    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Missing required 'domains' array for component '" + classname + "'");
    }
    for (const auto& d : j["domains"]) {
        if (!d.is_string()) {
            throw std::runtime_error("Invalid domain entry for component '" + classname + "': must be string");
        }
        domains.push_back(json_parser_internal::parse_domain_string(d.get<std::string>()));
    }
    if (domains.empty()) {
        throw std::runtime_error("Empty 'domains' array for component '" + classname + "'");
    }

    // Parse interface (ports)
    std::unordered_map<std::string, Port> ports;
    if (!j.contains("interface") || !j["interface"].is_array()) {
        throw std::runtime_error("Missing required 'interface' array for component '" + classname + "'");
    }
    for (const auto& p : j["interface"]) {
        if (!p.is_object()) {
            throw std::runtime_error("Invalid interface entry for component '" + classname + "': must be object");
        }
        if (!p.contains("name") || !p["name"].is_string() || p["name"].get<std::string>().empty()) {
            throw std::runtime_error("Interface entry missing required non-empty 'name' for component '" + classname + "'");
        }
        if (!p.contains("direction") || !p["direction"].is_number_integer()) {
            throw std::runtime_error("Interface entry missing required integer 'direction' for component '" + classname + "'");
        }
        if (!p.contains("domain") || !p["domain"].is_number_integer()) {
            throw std::runtime_error("Interface entry missing required integer 'domain' for component '" + classname + "'");
        }
        if (!p.contains("source_writer") || !p["source_writer"].is_boolean()) {
            throw std::runtime_error("Interface entry missing required boolean 'source_writer' for component '" + classname + "'");
        }
        if (!p.contains("type") || !p["type"].is_string()) {
            throw std::runtime_error("Interface entry missing required string 'type' for component '" + classname + "'");
        }

        Port port;
        int dir = p["direction"].get<int>();
        if (dir == 0) port.direction = bp2::Direction::Input;
        else if (dir == 1) port.direction = bp2::Direction::Output;
        else if (dir == 2) port.direction = bp2::Direction::InOut;
        else {
            throw std::runtime_error("Invalid interface direction value for component '" + classname + "'");
        }
        port.domain = json_parser_internal::parse_domain_mask_int(p["domain"].get<int>());
        port.source_writer = p["source_writer"].get<bool>();
        port.type = json_parser_internal::parse_port_type_string(p["type"].get<std::string>());

        ports[p["name"].get<std::string>()] = port;
    }

    // Parse params
    std::unordered_map<std::string, ParamSpec> params;
    json_parser_internal::merge_params_and_schema(j, "param_defaults", params);

    // Parse solver_role (only valid for primitives)
    std::optional<SolverRole> solver_role;
    if (j.contains("solver_role")) {
        if (!j["solver_role"].is_object()) {
            throw std::runtime_error("'solver_role' must be an object for component '" + classname + "'");
        }

        const auto& sr = j["solver_role"];
        SolverRole role;

        if (!sr.contains("kind") || !sr["kind"].is_string()) {
            throw std::runtime_error("solver_role missing required string 'kind' for component '" + classname + "'");
        }
        role.kind = sr["kind"].get<std::string>();

        if (sr.contains("ports")) {
            if (!sr["ports"].is_object()) {
                throw std::runtime_error("solver_role field 'ports' must be object for component '" + classname + "'");
            }
            for (const auto& [k, v] : sr["ports"].items()) {
                if (!v.is_string()) {
                    throw std::runtime_error("solver_role ports['" + k + "'] must be string for component '" + classname + "'");
                }
                role.port_map[k] = v.get<std::string>();
            }
        }

        if (sr.contains("params")) {
            if (!sr["params"].is_object()) {
                throw std::runtime_error("solver_role field 'params' must be object for component '" + classname + "'");
            }
            for (const auto& [k, v] : sr["params"].items()) {
                if (!v.is_string()) {
                    throw std::runtime_error("solver_role params['" + k + "'] must be string for component '" + classname + "'");
                }
                role.param_map[k] = v.get<std::string>();
            }
        }

        if (sr.contains("values")) {
            if (!sr["values"].is_object()) {
                throw std::runtime_error("solver_role field 'values' must be object for component '" + classname + "'");
            }
            for (const auto& [k, v] : sr["values"].items()) {
                if (!v.is_number()) {
                    throw std::runtime_error("solver_role values['" + k + "'] must be number for component '" + classname + "'");
                }
                role.value_map[k] = static_cast<float>(v.get<double>());
            }
        }

        solver_role = std::move(role);
    }

    // For non-cpp_class blueprints: convert v3 nodes/wires to devices/connections
    // so that parse_json_impl can expand composites automatically.
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    if (!is_cpp && j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node : j["nodes"]) {
            const std::string node_type = node.value("type", "");
            if (node.value("kind", "") == "bridge_port") {
                BridgePortDefinition bridge;
                bridge.id = node.value("id", "");
                bridge.exposed_port = node.value("exposed_port", bridge.id);
                bridge.direction = bridge_side_from_string(node.value("side", "input"));
                bridge.type = json_parser_internal::parse_port_type_string(node.value("port_type", "Contextual"));
                bridge.label = node.value("label", "");
                if (node.contains("layout") && node["layout"].is_object()) {
                    bridge.pos = {
                        node["layout"].value("x", 0.0f),
                        node["layout"].value("y", 0.0f)
                    };
                    if (node["layout"].contains("width") && node["layout"].contains("height")) {
                        bridge.size = {
                            node["layout"].value("width", 0.0f),
                            node["layout"].value("height", 0.0f)
                        };
                    }
                }
                bridge_ports.push_back(std::move(bridge));
                continue;
            }

            DeviceInstance dev;
            dev.name = node.value("id", "");
            dev.classname = node_type;
            if (node.contains("params") && node["params"].is_object()) {
                for (const auto& [k, v] : node["params"].items()) {
                    if (v.is_number()) {
                        dev.params[k] = json(v.get<double>()).dump();
                    } else if (v.is_string()) {
                        dev.params[k] = v.get<std::string>();
                    }
                }
            }
            if (node.contains("string_params") && node["string_params"].is_object()) {
                for (const auto& [k, v] : node["string_params"].items()) {
                    dev.params[k] = v.get<std::string>();
                }
            }
            if (node.contains("position") && node["position"].is_object()) {
                dev.pos = {
                    node["position"].value("x", 0.0f),
                    node["position"].value("y", 0.0f)
                };
            }
            if (node.contains("size") && node["size"].is_object()) {
                dev.size = {
                    node["size"].value("x", 0.0f),
                    node["size"].value("y", 0.0f)
                };
            }
            devices.push_back(std::move(dev));
        }

        if (j.contains("wires") && j["wires"].is_array()) {
            for (const auto& wire : j["wires"]) {
                Connection conn;
                std::string src = wire.value("source", "");
                std::string tgt = wire.value("target", "");
                // Convert "/node:port" → "node.port"
                if (!src.empty() && src[0] == '/') src = src.substr(1);
                if (!tgt.empty() && tgt[0] == '/') tgt = tgt.substr(1);
                std::replace(src.begin(), src.end(), ':', '.');
                std::replace(tgt.begin(), tgt.end(), ':', '.');
                conn.from = std::move(src);
                conn.to = std::move(tgt);
                if (wire.contains("routing_points") && wire["routing_points"].is_array()) {
                    for (const auto& rp : wire["routing_points"]) {
                        if (rp.contains("x") && rp.contains("y")) {
                            conn.routing_points.push_back({rp["x"].get<float>(), rp["y"].get<float>()});
                        }
                    }
                }
                connections.push_back(std::move(conn));
            }
        }
    }

    // Construct the appropriate variant based on cpp_class
    if (is_cpp) {
        PrimitiveSpec prim;
        prim.classname = std::move(classname);
        prim.ports = std::move(ports);
        prim.params = std::move(params);
        prim.domains = std::move(domains);
        prim.execution = std::nullopt; // not parsed from library blueprints
        prim.scheduler_source = scheduler_source;
        prim.solver_owned_electrical = solver_owned_electrical;
        prim.solver_role = std::move(solver_role);
        prim.priority = std::move(priority);
        prim.critical = critical;
        prim.visual_only = visual_only;
        return {ComponentSpec{std::move(prim)}, std::move(pres)};
    } else {
        CompositeSpec comp;
        comp.classname = std::move(classname);
        comp.ports = std::move(ports);
        comp.params = std::move(params);
        comp.domains = std::move(domains);
        comp.scheduler_source = scheduler_source;
        comp.solver_owned_electrical = solver_owned_electrical;
        comp.priority = std::move(priority);
        comp.critical = critical;
        comp.visual_only = visual_only;
        comp.devices = std::move(devices);
        comp.connections = std::move(connections);
        comp.bridge_ports = std::move(bridge_ports);
        return {ComponentSpec{std::move(comp)}, std::move(pres)};
    }
}

} // namespace

ComponentRegistry load_component_registry(const std::string& library_dir) {
    ComponentRegistry registry;

    std::filesystem::path library_path(library_dir);
    if (!std::filesystem::exists(library_path) && library_path.is_relative()) {
        std::vector<std::filesystem::path> try_paths = {
            library_path,
            "../" / library_path,
            "../../" / library_path,
            "../../../" / library_path,
        };

        for (const auto& path : try_paths) {
            if (std::filesystem::exists(path)) {
                library_path = path;
                break;
            }
        }
    }

    if (!std::filesystem::exists(library_path)) {
        spdlog::warn("[json_parser] Library directory '{}' does not exist, using empty registry", library_dir);
        return registry;
    }

    size_t loaded_count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".blueprint") {
            continue;
        }
        try {
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

            json j = json::parse(content);
            if (!j.contains("version") || !j["version"].is_string()
                || j["version"].get<std::string>() != "3.0") {
                throw std::runtime_error("Invalid or unsupported 'version' in '" + entry.path().string() + "'");
            }

            auto [spec, pres] = parse_blueprint_type_definition(j, entry.path());
            std::string classname = spec_classname(spec);

            if (registry.has(classname)) {
                throw std::runtime_error("Duplicate classname '" + classname + "' in '" + entry.path().string() + "'");
            }

            auto relative_dir = std::filesystem::relative(entry.path().parent_path(), library_path);
            std::string category = relative_dir.generic_string();
            if (category == ".") category = "";

            registry.types[classname] = std::move(spec);
            registry.presentation.specs[classname] = std::move(pres);
            if (!category.empty()) {
                registry.catalog.categories[classname] = category;
            }
            loaded_count++;

            spdlog::debug("[json_parser] Loaded type definition: '{}' from {} (category: {})",
                classname, entry.path().filename().string(), category.empty() ? "root" : category);
        } catch (const std::exception& e) {
            spdlog::error("[json_parser] Failed to parse type definition '{}': {}",
                entry.path().string(), e.what());
            throw;
        }
    }

    spdlog::info("[json_parser] Loaded {} type definitions from '{}'", loaded_count, library_dir);
    return registry;
}
