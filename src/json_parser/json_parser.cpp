#include "json_parser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace an24 {

// Helper: convert string to Domain
static Domain parse_domain(const std::string& s) {
    if (s == "Electrical") return Domain::Electrical;
    if (s == "Hydraulic") return Domain::Hydraulic;
    if (s == "Mechanical") return Domain::Mechanical;
    if (s == "Thermal") return Domain::Thermal;
    throw std::runtime_error("Unknown domain: " + s);
}

// Helper: convert Domain to string
static std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Thermal: return "Thermal";
    }
    return "Unknown";
}

// Helper: convert string to PortDirection
static PortDirection parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i") return PortDirection::In;
    if (s == "out" || s == "output" || s == "o") return PortDirection::Out;
    return PortDirection::Out;  // default
}

// Helper: parse a single port
static Port parse_port(const json& j) {
    Port port;
    if (j.is_string()) {
        port.direction = parse_port_direction(j.get<std::string>());
    } else if (j.is_object()) {
        if (j.contains("direction")) {
            port.direction = parse_port_direction(j["direction"].get<std::string>());
        }
    }
    return port;
}

// Helper: parse DeviceInstance from JSON
static DeviceInstance parse_device(const json& j) {
    DeviceInstance dev;

    if (j.contains("name")) dev.name = j["name"].get<std::string>();
    if (j.contains("template")) dev.template_name = j["template"].get<std::string>();
    if (j.contains("internal")) dev.internal = j["internal"].get<std::string>();
    if (j.contains("priority")) dev.priority = j["priority"].get<std::string>();
    if (j.contains("bucket") && !j["bucket"].is_null()) {
        dev.bucket = j["bucket"].get<size_t>();
    }
    if (j.contains("critical")) dev.critical = j["critical"].get<bool>();
    if (j.contains("is_composite")) dev.is_composite = j["is_composite"].get<bool>();

    // Ports
    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            dev.ports[port_name] = parse_port(port_val);
        }
    }

    // Params (key-value pairs)
    if (j.contains("params")) {
        for (auto& [key, val] : j["params"].items()) {
            dev.params[key] = val.get<std::string>();
        }
    }

    // Domains
    if (j.contains("domain")) {
        std::vector<Domain> domains;
        if (j["domain"].is_string()) {
            // Single domain: "Electrical" or comma-separated: "Electrical,Hydraulic"
            std::string s = j["domain"].get<std::string>();
            size_t start = 0;
            while (start < s.size()) {
                size_t comma = s.find(',', start);
                std::string part = s.substr(start, comma - start);
                // Trim whitespace
                while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(part.begin());
                while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
                if (!part.empty()) {
                    domains.push_back(parse_domain(part));
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (j["domain"].is_array()) {
            for (const auto& d : j["domain"]) {
                domains.push_back(parse_domain(d.get<std::string>()));
            }
        }
        if (!domains.empty()) {
            dev.explicit_domains = domains;
        }
    }

    return dev;
}

// Helper: parse Connection from JSON
static Connection parse_connection(const json& j) {
    Connection conn;
    if (j.is_string()) {
        // Format: "from -> to" or just two-element array
        std::string s = j.get<std::string>();
        size_t arrow = s.find("->");
        if (arrow != std::string::npos) {
            conn.from = s.substr(0, arrow);
            conn.to = s.substr(arrow + 2);
            // Trim whitespace
            while (!conn.from.empty() && conn.from[0] == ' ') conn.from.erase(conn.from.begin());
            while (!conn.to.empty() && conn.to[0] == ' ') conn.to.erase(conn.to.begin());
            while (!conn.from.empty() && conn.from.back() == ' ') conn.from.pop_back();
            while (!conn.to.empty() && conn.to.back() == ' ') conn.to.pop_back();
        }
    } else if (j.is_object()) {
        if (j.contains("from")) conn.from = j["from"].get<std::string>();
        if (j.contains("to")) conn.to = j["to"].get<std::string>();
    }
    return conn;
}

// Helper: parse SubsystemCall from JSON
static SubsystemCall parse_subsystem(const json& j) {
    SubsystemCall sub;
    if (j.contains("name")) sub.name = j["name"].get<std::string>();
    if (j.contains("template")) sub.template_name = j["template"].get<std::string>();
    if (j.contains("port_map")) {
        for (auto& [external, internal] : j["port_map"].items()) {
            sub.port_map[external] = internal.get<std::string>();
        }
    }
    return sub;
}

// Helper: parse SystemTemplate from JSON
static SystemTemplate parse_template(const json& j) {
    SystemTemplate tpl;
    if (j.contains("name")) tpl.name = j["name"].get<std::string>();

    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            tpl.devices.push_back(parse_device(dev_j));
        }
    }

    if (j.contains("subsystems")) {
        for (const auto& sub_j : j["subsystems"]) {
            tpl.subsystems.push_back(parse_subsystem(sub_j));
        }
    }

    if (j.contains("exposed_ports")) {
        for (auto& [external, internal] : j["exposed_ports"].items()) {
            tpl.exposed_ports[external] = internal.get<std::string>();
        }
    }

    if (j.contains("domain")) {
        // Parse domain similar to DeviceInstance
        std::vector<Domain> domains;
        json::const_reference dom = j["domain"];
        if (dom.is_string()) {
            std::string s = dom.get<std::string>();
            size_t start = 0;
            while (start < s.size()) {
                size_t comma = s.find(',', start);
                std::string part = s.substr(start, comma - start);
                while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(part.begin());
                while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
                if (!part.empty()) domains.push_back(parse_domain(part));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (dom.is_array()) {
            for (const auto& d : dom) {
                domains.push_back(parse_domain(d.get<std::string>()));
            }
        }
        if (!domains.empty()) tpl.explicit_domains = domains;
    }

    return tpl;
}

ParserContext parse_json(const std::string& json_text) {
    spdlog::debug("[json_parser] Parsing JSON text");

    auto j = json::parse(json_text);
    ParserContext ctx;

    // Parse templates
    if (j.contains("templates")) {
        for (auto& [name, tpl_j] : j["templates"].items()) {
            auto tpl = parse_template(tpl_j);
            if (tpl.name.empty()) tpl.name = name;
            ctx.templates[tpl.name] = tpl;
        }
    }

    // Parse devices (also accepts "top_level_devices")
    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            ctx.devices.push_back(parse_device(dev_j));
        }
    } else if (j.contains("top_level_devices")) {
        for (const auto& dev_j : j["top_level_devices"]) {
            ctx.devices.push_back(parse_device(dev_j));
        }
    }

    // Parse connections
    if (j.contains("connections")) {
        for (const auto& conn_j : j["connections"]) {
            ctx.connections.push_back(parse_connection(conn_j));
        }
    }

    spdlog::debug("[json_parser] Parsed {} templates, {} devices, {} connections",
        ctx.templates.size(), ctx.devices.size(), ctx.connections.size());

    return ctx;
}

// Serialization helpers
static json port_to_json(const Port& port) {
    json j;
    j["direction"] = (port.direction == PortDirection::In) ? "in" : "out";
    return j;
}

static json device_to_json(const DeviceInstance& dev) {
    json j;
    if (!dev.name.empty()) j["name"] = dev.name;
    if (!dev.template_name.empty()) j["template"] = dev.template_name;
    if (!dev.internal.empty()) j["internal"] = dev.internal;
    if (dev.priority != "med") j["priority"] = dev.priority;
    if (dev.bucket.has_value()) j["bucket"] = dev.bucket.value();
    if (dev.critical) j["critical"] = true;
    if (dev.is_composite) j["is_composite"] = true;

    if (!dev.ports.empty()) {
        json ports;
        for (const auto& [name, port] : dev.ports) {
            ports[name] = port_to_json(port);
        }
        j["ports"] = ports;
    }

    if (!dev.params.empty()) {
        json params;
        for (const auto& [key, val] : dev.params) {
            params[key] = val;
        }
        j["params"] = params;
    }

    if (dev.explicit_domains.has_value()) {
        std::string domains_str;
        for (size_t i = 0; i < dev.explicit_domains->size(); ++i) {
            if (i > 0) domains_str += ",";
            domains_str += domain_to_string((*dev.explicit_domains)[i]);
        }
        j["domain"] = domains_str;
    }

    return j;
}

static json connection_to_json(const Connection& conn) {
    json j;
    j["from"] = conn.from;
    j["to"] = conn.to;
    return j;
}

static json subsystem_to_json(const SubsystemCall& sub) {
    json j;
    if (!sub.name.empty()) j["name"] = sub.name;
    j["template"] = sub.template_name;
    if (!sub.port_map.empty()) {
        json port_map;
        for (const auto& [ext, int_] : sub.port_map) {
            port_map[ext] = int_;
        }
        j["port_map"] = port_map;
    }
    return j;
}

static json template_to_json(const SystemTemplate& tpl) {
    json j;
    if (!tpl.name.empty()) j["name"] = tpl.name;

    if (!tpl.devices.empty()) {
        json devices;
        for (const auto& dev : tpl.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    if (!tpl.subsystems.empty()) {
        json subsystems;
        for (const auto& sub : tpl.subsystems) {
            subsystems.push_back(subsystem_to_json(sub));
        }
        j["subsystems"] = subsystems;
    }

    if (!tpl.exposed_ports.empty()) {
        json exposed;
        for (const auto& [ext, int_] : tpl.exposed_ports) {
            exposed[ext] = int_;
        }
        j["exposed_ports"] = exposed;
    }

    if (tpl.explicit_domains.has_value()) {
        std::string domains_str;
        for (size_t i = 0; i < tpl.explicit_domains->size(); ++i) {
            if (i > 0) domains_str += ",";
            domains_str += domain_to_string((*tpl.explicit_domains)[i]);
        }
        j["domain"] = domains_str;
    }

    return j;
}

std::string serialize_json(const ParserContext& ctx) {
    json j;

    // Templates
    if (!ctx.templates.empty()) {
        json templates;
        for (const auto& [name, tpl] : ctx.templates) {
            templates[name] = template_to_json(tpl);
        }
        j["templates"] = templates;
    }

    // Devices
    if (!ctx.devices.empty()) {
        json devices;
        for (const auto& dev : ctx.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    // Connections
    if (!ctx.connections.empty()) {
        json connections;
        for (const auto& conn : ctx.connections) {
            connections.push_back(connection_to_json(conn));
        }
        j["connections"] = connections;
    }

    return j.dump(2);  // Pretty print with 2-space indent
}

} // namespace an24
