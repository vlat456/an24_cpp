#include "json_parser.h"

#include "../parse_number.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {

Domain parse_domain(const std::string& s) {
    if (s == "Electrical") return Domain::Electrical;
    if (s == "Logical") return Domain::Logical;
    if (s == "Hydraulic") return Domain::Hydraulic;
    if (s == "Mechanical") return Domain::Mechanical;
    if (s == "Thermal") return Domain::Thermal;
    throw std::runtime_error("Unknown domain: " + s);
}

Domain parse_domain_mask_int(int v) {
    if (v <= 0 || (v & ~31) != 0) {
        throw std::runtime_error("Invalid domain bitmask value: " + std::to_string(v));
    }
    return static_cast<Domain>(static_cast<uint8_t>(v));
}

ParamSchemaType parse_param_schema_type(const std::string& s) {
    if (s == "float") return ParamSchemaType::Float;
    if (s == "int") return ParamSchemaType::Int;
    if (s == "bool") return ParamSchemaType::Bool;
    if (s == "string") return ParamSchemaType::String;
    throw std::runtime_error("Unknown param schema type: " + s);
}

std::unordered_map<std::string, ParamSchemaEntry> parse_param_schema(const json& j) {
    std::unordered_map<std::string, ParamSchemaEntry> out;
    if (!j.is_object()) {
        throw std::runtime_error("'param_schema' must be an object");
    }
    for (const auto& [name, entry] : j.items()) {
        if (!entry.is_object()) {
            throw std::runtime_error("param_schema entry '" + name + "' must be object");
        }
        if (!entry.contains("type") || !entry["type"].is_string()) {
            throw std::runtime_error("param_schema entry '" + name + "' missing string 'type'");
        }
        ParamSchemaEntry e;
        e.type = parse_param_schema_type(entry["type"].get<std::string>());
        if (entry.contains("required")) {
            if (!entry["required"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'required' must be bool");
            }
            e.required = entry["required"].get<bool>();
        }
        if (entry.contains("min")) {
            if (!entry["min"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'min' must be number");
            }
            e.min = entry["min"].get<double>();
        }
        if (entry.contains("max")) {
            if (!entry["max"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'max' must be number");
            }
            e.max = entry["max"].get<double>();
        }
        if (e.min.has_value() && e.max.has_value() && *e.min > *e.max) {
            throw std::runtime_error("param_schema entry '" + name + "' has min > max");
        }
        if (entry.contains("visual_only")) {
            if (!entry["visual_only"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'visual_only' must be bool");
            }
            e.visual_only = entry["visual_only"].get<bool>();
        }
        out[name] = e;
    }
    return out;
}

PortType parse_port_type(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Signal") return PortType::Any;
    if (s == "Fraction") return PortType::Any;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Any") return PortType::Any;
    throw std::runtime_error("Unknown port type: " + s);
}

TypeDefinition parse_blueprint_type_definition(const json& j, const std::filesystem::path& path) {
    TypeDefinition def;
    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty()) {
        throw std::runtime_error("Missing required non-empty 'id' in '" + path.string() + "'");
    }
    def.classname = j["id"].get<std::string>();

    if (j.contains("display_name") && j["display_name"].is_string()) {
        def.description = j["display_name"].get<std::string>();
    } else if (j.contains("description") && j["description"].is_string()) {
        def.description = j["description"].get<std::string>();
    }

    if (!j.contains("cpp_class") || !j["cpp_class"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'cpp_class' for component '" + def.classname + "'");
    }
    def.cpp_class = j["cpp_class"].get<bool>();

    if (j.contains("priority") && j["priority"].is_string()) {
        def.priority = j["priority"].get<std::string>();
    }
    if (j.contains("critical") && j["critical"].is_boolean()) {
        def.critical = j["critical"].get<bool>();
    }
    if (j.contains("content_type") && j["content_type"].is_string()) {
        def.content_type = j["content_type"].get<std::string>();
    }
    if (j.contains("render_hint") && j["render_hint"].is_string()) {
        def.render_hint = j["render_hint"].get<std::string>();
    }
    if (j.contains("visual_only") && j["visual_only"].is_boolean()) {
        def.visual_only = j["visual_only"].get<bool>();
    }
    if (!j.contains("scheduler_source") || !j["scheduler_source"].is_boolean()) {
        throw std::runtime_error("Missing required boolean 'scheduler_source' for component '" + def.classname + "'");
    }
    def.scheduler_source = j["scheduler_source"].get<bool>();

    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Missing required 'domains' array for component '" + def.classname + "'");
    }
    {
        std::vector<Domain> domains;
        for (const auto& d : j["domains"]) {
            if (!d.is_string()) {
                throw std::runtime_error("Invalid domain entry for component '" + def.classname + "': must be string");
            }
            domains.push_back(parse_domain(d.get<std::string>()));
        }
        if (domains.empty()) {
            throw std::runtime_error("Empty 'domains' array for component '" + def.classname + "'");
        }
        def.domains = std::move(domains);
    }

    if (!j.contains("interface") || !j["interface"].is_array()) {
        throw std::runtime_error("Missing required 'interface' array for component '" + def.classname + "'");
    }
    for (const auto& p : j["interface"]) {
        if (!p.is_object()) {
            throw std::runtime_error("Invalid interface entry for component '" + def.classname + "': must be object");
        }
        if (!p.contains("name") || !p["name"].is_string() || p["name"].get<std::string>().empty()) {
            throw std::runtime_error("Interface entry missing required non-empty 'name' for component '" + def.classname + "'");
        }
        if (!p.contains("direction") || !p["direction"].is_number_integer()) {
            throw std::runtime_error("Interface entry missing required integer 'direction' for component '" + def.classname + "'");
        }
        if (!p.contains("domain") || !p["domain"].is_number_integer()) {
            throw std::runtime_error("Interface entry missing required integer 'domain' for component '" + def.classname + "'");
        }
        if (!p.contains("source_writer") || !p["source_writer"].is_boolean()) {
            throw std::runtime_error("Interface entry missing required boolean 'source_writer' for component '" + def.classname + "'");
        }
        if (!p.contains("type") || !p["type"].is_string()) {
            throw std::runtime_error("Interface entry missing required string 'type' for component '" + def.classname + "'");
        }

        Port port;
        int dir = p["direction"].get<int>();
        if (dir == 0) port.direction = PortDirection::In;
        else if (dir == 1) port.direction = PortDirection::Out;
        else if (dir == 2) port.direction = PortDirection::InOut;
        else {
            throw std::runtime_error("Invalid interface direction value for component '" + def.classname + "'");
        }
        port.domain = parse_domain_mask_int(p["domain"].get<int>());
        port.source_writer = p["source_writer"].get<bool>();
        port.type = parse_port_type(p["type"].get<std::string>());

        def.ports[p["name"].get<std::string>()] = port;
    }

    if (j.contains("param_defaults") && j["param_defaults"].is_object()) {
        for (auto& [k, v] : j["param_defaults"].items()) {
            if (v.is_string()) {
                def.params[k] = v.get<std::string>();
            } else if (v.is_number()) {
                def.params[k] = locale_safe::format_float(static_cast<float>(v.get<double>()));
            }
        }
    }
    if (j.contains("param_schema")) {
        def.param_schema = parse_param_schema(j["param_schema"]);
    }

    // For non-cpp_class blueprints: convert v3 nodes/wires to devices/connections
    // so that parse_json_impl can expand composites automatically.
    if (!def.cpp_class && j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node : j["nodes"]) {
            DeviceInstance dev;
            dev.name = node.value("id", "");
            dev.classname = node.value("type", "");
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
            def.devices.push_back(std::move(dev));
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
                def.connections.push_back(std::move(conn));
            }
        }
    }

    return def;
}

} // namespace

TypeRegistry load_type_registry(const std::string& library_dir) {
    TypeRegistry registry;

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

            TypeDefinition def = parse_blueprint_type_definition(j, entry.path());

            if (registry.has(def.classname)) {
                throw std::runtime_error("Duplicate classname '" + def.classname + "' in '" + entry.path().string() + "'");
            }

            auto relative_dir = std::filesystem::relative(entry.path().parent_path(), library_path);
            std::string category = relative_dir.generic_string();
            if (category == ".") category = "";

            registry.types[def.classname] = def;
            if (!category.empty()) {
                registry.categories[def.classname] = category;
            }
            loaded_count++;

            spdlog::debug("[json_parser] Loaded type definition: '{}' from {} (category: {})",
                def.classname, entry.path().filename().string(), category.empty() ? "root" : category);
        } catch (const std::exception& e) {
            spdlog::error("[json_parser] Failed to parse type definition '{}': {}",
                entry.path().string(), e.what());
            throw;
        }
    }

    spdlog::info("[json_parser] Loaded {} type definitions from '{}'", loaded_count, library_dir);
    return registry;
}
