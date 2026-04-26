#include "io/json/component_registry_json_loader.h"

#include "io/json/json_internal_utils.h"
#include "io/json/type_definition_json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unordered_set>

using json = nlohmann::json;

namespace {

bp2::BridgeDirection bridge_direction_from_string(const std::string& direction) {
    if (direction == "input") return bp2::BridgeDirection::Input;
    if (direction == "output") return bp2::BridgeDirection::Output;
    throw std::runtime_error("Invalid bridge direction '" + direction + "'");
}

std::pair<ComponentSpec, TypePresentation> parse_blueprint_type_definition(
    const json& j,
    const std::filesystem::path& path)
{
    TypePresentation pres;

    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty()) {
        throw std::runtime_error("Missing required non-empty 'id' in '" + path.string() + "'");
    }
    std::string classname = j["id"].get<std::string>();

    if (j.contains("display_name") && j["display_name"].is_string()) {
        pres.description = j["display_name"].get<std::string>();
    } else if (j.contains("description") && j["description"].is_string()) {
        pres.description = j["description"].get<std::string>();
    }

    if (!j.contains("cpp_class") || !j["cpp_class"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'cpp_class' for component '" + classname + "'");
    }
    const bool is_cpp = j["cpp_class"].get<bool>();

    std::string priority = "med";
    if (j.contains("priority") && j["priority"].is_string()) {
        priority = j["priority"].get<std::string>();
    }

    bool critical = false;
    if (j.contains("critical") && j["critical"].is_boolean()) {
        critical = j["critical"].get<bool>();
    }

    if (j.contains("content_type") && j["content_type"].is_string()) {
        pres.content_type = bp2::parse_node_content_type(j["content_type"].get<std::string_view>());
    }
    if (j.contains("render_hint") && j["render_hint"].is_string()) {
        pres.render_hint = j["render_hint"].get<std::string>();
    }

    bool visual_only = false;
    if (j.contains("visual_only") && j["visual_only"].is_boolean()) {
        visual_only = j["visual_only"].get<bool>();
    }

    if (!j.contains("scheduler_source") || !j["scheduler_source"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'scheduler_source' for component '" + classname + "'");
    }
    const bool scheduler_source = j["scheduler_source"].get<bool>();

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

    std::vector<Domain> domains;
    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Missing required 'domains' array for component '" + classname + "'");
    }
    for (const auto& d : j["domains"]) {
        if (!d.is_string()) {
            throw std::runtime_error("Invalid domain entry for component '" + classname + "': must be string");
        }
        domains.push_back(json_io_internal::parse_domain_string(d.get<std::string>()));
    }
    if (domains.empty()) {
        throw std::runtime_error("Empty 'domains' array for component '" + classname + "'");
    }

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
        const int dir = p["direction"].get<int>();
        if (dir == 0) port.direction = bp2::Direction::Input;
        else if (dir == 1) port.direction = bp2::Direction::Output;
        else if (dir == 2) port.direction = bp2::Direction::InOut;
        else throw std::runtime_error("Invalid interface direction value for component '" + classname + "'");
        port.domain = json_io_internal::parse_domain_mask_int(p["domain"].get<int>());
        port.source_writer = p["source_writer"].get<bool>();
        port.type = json_io_internal::parse_port_type_string(p["type"].get<std::string>());

        ports[p["name"].get<std::string>()] = port;
    }

    std::unordered_map<std::string, ParamSpec> params;
    json_io_internal::merge_params_and_schema(j, "param_defaults", params);

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
        role.kind = parse_solver_role_kind(sr["kind"].get<std::string>());

        if (sr.contains("domain") && sr["domain"].is_string()) {
            role.domain = json_io_internal::parse_domain_string(sr["domain"].get<std::string>());
        }

        if (sr.contains("ports")) {
            if (!sr["ports"].is_object()) {
                throw std::runtime_error("solver_role field 'ports' must be object for component '" + classname + "'");
            }
            for (const auto& [key, value] : sr["ports"].items()) {
                if (!value.is_string()) {
                    throw std::runtime_error("solver_role ports['" + key + "'] must be string for component '" + classname + "'");
                }
                role.port_map[key] = value.get<std::string>();
            }
        }

        if (sr.contains("params")) {
            if (!sr["params"].is_object()) {
                throw std::runtime_error("solver_role field 'params' must be object for component '" + classname + "'");
            }
            for (const auto& [key, value] : sr["params"].items()) {
                if (!value.is_string()) {
                    throw std::runtime_error("solver_role params['" + key + "'] must be string for component '" + classname + "'");
                }
                role.param_map[key] = value.get<std::string>();
            }
        }

        if (sr.contains("values")) {
            if (!sr["values"].is_object()) {
                throw std::runtime_error("solver_role field 'values' must be object for component '" + classname + "'");
            }
            for (const auto& [key, value] : sr["values"].items()) {
                if (!value.is_number()) {
                    throw std::runtime_error("solver_role values['" + key + "'] must be number for component '" + classname + "'");
                }
                role.value_map[key] = static_cast<float>(value.get<double>());
            }
        }

        solver_role = std::move(role);
    }

    std::vector<DeviceInstance> devices;
    std::vector<RoutedConnection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    if (!is_cpp && j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node : j["nodes"]) {
            const std::string node_type = node.value("type", "");
            if (node.value("kind", "") == "bridge_port") {
                static const std::unordered_set<std::string> allowed_bridge_fields = {
                    "id", "kind", "exposed_port", "direction", "port_type", "layout", "label"
                };
                if (!node.is_object()) {
                    throw std::runtime_error("Invalid bridge node for component '" + classname + "': must be object");
                }
                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (allowed_bridge_fields.find(it.key()) == allowed_bridge_fields.end()) {
                        throw std::runtime_error("Unknown bridge node field '" + it.key() + "' for component '" + classname + "'");
                    }
                }
                BridgePortDefinition bridge;
                if (!node.contains("id") || !node["id"].is_string() || node["id"].get<std::string>().empty()) {
                    throw std::runtime_error("Bridge node missing required non-empty string 'id' for component '" + classname + "'");
                }
                bridge.id = node["id"].get<std::string>();
                if (!node.contains("exposed_port") || !node["exposed_port"].is_string()
                    || node["exposed_port"].get<std::string>().empty()) {
                    throw std::runtime_error("Bridge node missing required non-empty string 'exposed_port' for component '" + classname + "'");
                }
                bridge.exposed_port = node["exposed_port"].get<std::string>();
                if (!node.contains("direction") || !node["direction"].is_string()) {
                    throw std::runtime_error("Bridge node missing required string 'direction' for component '" + classname + "'");
                }
                bridge.direction = bridge_direction_from_string(node["direction"].get<std::string>());
                if (!node.contains("port_type") || !node["port_type"].is_string()) {
                    throw std::runtime_error("Bridge node missing required string 'port_type' for component '" + classname + "'");
                }
                bridge.type = json_io_internal::parse_port_type_string(node["port_type"].get<std::string>());
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
                for (const auto& [key, value] : node["params"].items()) {
                    if (value.is_number()) {
                        dev.params[key] = json(value.get<double>()).dump();
                    } else if (value.is_string()) {
                        dev.params[key] = value.get<std::string>();
                    }
                }
            }
            if (node.contains("string_params") && node["string_params"].is_object()) {
                for (const auto& [key, value] : node["string_params"].items()) {
                    dev.params[key] = value.get<std::string>();
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
                RoutedConnection conn;
                std::string src = wire.value("source", "");
                std::string tgt = wire.value("target", "");
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

    // visual_only goes to TypePresentation
    pres.visual_only = visual_only;

    if (is_cpp) {
        PrimitiveSpec prim;
        prim.classname = std::move(classname);
        prim.ports = std::move(ports);
        prim.params = std::move(params);
        prim.domains = std::move(domains);
        prim.solver.scheduler_source = scheduler_source;
        prim.solver.solver_owned_electrical = solver_owned_electrical;
        prim.solver.solver_role = std::move(solver_role);
        prim.priority = std::move(priority);
        prim.critical = critical;
        return {ComponentSpec{std::move(prim)}, std::move(pres)};
    }

    CompositeSpec comp;
    comp.classname = std::move(classname);
    comp.ports = std::move(ports);
    comp.params = std::move(params);
    comp.domains = std::move(domains);
    comp.priority = std::move(priority);
    comp.critical = critical;
    comp.devices = std::move(devices);
    comp.connections = std::move(connections);
    comp.bridge_ports = std::move(bridge_ports);
    return {ComponentSpec{std::move(comp)}, std::move(pres)};
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
        spdlog::warn("[json_io] Library directory '{}' does not exist, using empty registry", library_dir);
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

            registry.register_type(classname, std::move(spec), std::move(pres), std::move(category));
            loaded_count++;

            spdlog::debug("[json_io] Loaded type definition: '{}' from {} (category: {})",
                classname, entry.path().filename().string(), category.empty() ? "root" : category);
        } catch (const std::exception& e) {
            spdlog::error("[json_io] Failed to parse type definition '{}': {}",
                entry.path().string(), e.what());
            throw;
        }
    }

    spdlog::info("[json_io] Loaded {} type definitions from '{}'", loaded_count, library_dir);
    return registry;
}
