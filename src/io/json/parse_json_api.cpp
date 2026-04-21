#include "io/json/parse_json_api.h"

#include "core/registry/component_resolution.h"
#include "io/json/component_registry_json_loader.h"
#include "io/json/json_parse_internal.h"

#include "core/solvers/common/signal_key.h"

#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {

SubsystemCall parse_subsystem(const json& j) {
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

SystemTemplate parse_template(const json& j) {
    SystemTemplate tpl;
    if (j.contains("name")) tpl.name = j["name"].get<std::string>();

    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            tpl.devices.push_back(json_io_detail::parse_device(dev_j));
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

    return tpl;
}

void expand_composite_into(
    ParserContext& ctx,
    const DeviceInstance& raw_dev,
    const CompositeSpec& comp,
    const ComponentRegistry& registry,
    std::set<std::string>& expanding)
{
    if (!expanding.insert(raw_dev.classname).second) {
        throw std::runtime_error("Blueprint cycle detected: '" + raw_dev.classname +
            "' is already being expanded (circular dependency)");
    }

    struct ExpansionScopeGuard {
        std::set<std::string>& expanding;
        const std::string& classname;

        ~ExpansionScopeGuard() {
            expanding.erase(classname);
        }
    } guard{expanding, raw_dev.classname};

    spdlog::info("[json_io] Expanding blueprint type '{}' as device '{}' from ComponentRegistry",
        raw_dev.classname, raw_dev.name);

    for (const auto& inner_dev : comp.devices) {
        const auto* inner_def = registry.get(inner_dev.classname);
        if (!inner_def) {
            throw std::runtime_error("Component definition not found: " + inner_dev.classname);
        }

        const auto* inner_comp = as_composite(*inner_def);
        if (inner_comp && !inner_comp->devices.empty()) {
            DeviceInstance prefixed_dev = inner_dev;
            prefixed_dev.name = inner_dev.name.empty()
                ? raw_dev.name
                : signal_key::make_child_scope_key(raw_dev.name, inner_dev.name);
            expand_composite_into(ctx, prefixed_dev, *inner_comp, registry, expanding);
        } else {
            // Skip visual-only devices at expansion boundary
            if (auto* pres = registry.presentation.get(inner_dev.classname)) {
                if (pres->visual_only) {
                    spdlog::debug("[json_io] Skipping visual-only inner device '{}' of type '{}'",
                        inner_dev.name.empty() ? raw_dev.name : inner_dev.name, inner_dev.classname);
                    continue;
                }
            }
            ResolvedDevice resolved = resolve_component(inner_dev, *inner_def);
            auto error = ctx.registry.validate_instance(resolved);
            if (error.has_value()) {
                throw std::runtime_error("Device validation failed: " + error.value());
            }
            resolved.name = inner_dev.name.empty()
                ? raw_dev.name
                : signal_key::make_child_scope_key(raw_dev.name, inner_dev.name);
            ctx.devices.push_back(std::move(resolved));
        }
    }

    for (const auto& conn : comp.connections) {
        Connection rewritten;
        rewritten.from = signal_key::make_child_scope_key(raw_dev.name, conn.from);
        rewritten.to = signal_key::make_child_scope_key(raw_dev.name, conn.to);
        ctx.connections.push_back(std::move(rewritten));
    }

    for (auto bridge : comp.bridge_ports) {
        bridge.id = signal_key::make_child_scope_key(raw_dev.name, bridge.id);
        ctx.bridge_ports.push_back(std::move(bridge));
    }

    spdlog::info("[json_io] Expanded blueprint '{}' as device '{}' ({} inner devices)",
        raw_dev.classname, raw_dev.name, comp.devices.size());
}

ParserContext parse_json_impl(
    const std::string& json_text,
    ComponentRegistry& registry,
    std::set<std::string> expanding)
{
    spdlog::debug("[json_io] Parsing JSON text");
    auto j = json::parse(json_text);
    ParserContext ctx;
    ctx.registry = registry;

    if (j.contains("templates")) {
        for (auto& [name, tpl_j] : j["templates"].items()) {
            auto tpl = parse_template(tpl_j);
            if (tpl.name.empty()) tpl.name = name;
            ctx.templates[tpl.name] = tpl;
        }
    }

    std::vector<DeviceInstance> raw_devices;
    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            raw_devices.push_back(json_io_detail::parse_device(dev_j));
        }
    } else if (j.contains("top_level_devices")) {
        for (const auto& dev_j : j["top_level_devices"]) {
            raw_devices.push_back(json_io_detail::parse_device(dev_j));
        }
    }

    for (const auto& raw_dev : raw_devices) {
        if (!ctx.registry.has(raw_dev.classname)) {
            spdlog::error("[json_io] Unknown component classname '{}' in device '{}'",
                raw_dev.classname, raw_dev.name);
            throw std::runtime_error("Unknown component classname: " + raw_dev.classname);
        }

        const auto* def = ctx.registry.get(raw_dev.classname);
        if (!def) {
            spdlog::error("[json_io] Component definition not found for '{}' in device '{}'",
                raw_dev.classname, raw_dev.name);
            throw std::runtime_error("Component definition not found: " + raw_dev.classname);
        }

        const auto* comp = as_composite(*def);
        if (comp && !comp->devices.empty()) {
            expand_composite_into(ctx, raw_dev, *comp, registry, expanding);
            continue;
        }

        // Skip visual-only devices at resolution boundary
        if (auto* pres = registry.presentation.get(raw_dev.classname)) {
            if (pres->visual_only) {
                spdlog::debug("[json_io] Skipping visual-only device '{}' of type '{}'",
                    raw_dev.name, raw_dev.classname);
                continue;
            }
        }

        ResolvedDevice merged = resolve_component(raw_dev, *def);
        auto error = ctx.registry.validate_instance(merged);
        if (error.has_value()) {
            spdlog::error("[json_io] Validation failed for device '{}': {}", merged.name, error.value());
            throw std::runtime_error("Device validation failed: " + error.value());
        }

        ctx.devices.push_back(std::move(merged));
        spdlog::debug("[json_io] Merged device '{}' of type '{}' with component definition",
            raw_dev.name, raw_dev.classname);
    }

    if (j.contains("connections")) {
        for (const auto& conn_j : j["connections"]) {
            ctx.connections.push_back(json_io_detail::parse_connection(conn_j));
        }
    }

    if (j.contains("initial_values")) {
        if (!j["initial_values"].is_object()) {
            throw std::runtime_error("'initial_values' must be an object");
        }
        for (const auto& [port_ref, value] : j["initial_values"].items()) {
            if (!value.is_number()) {
                throw std::runtime_error("initial_values entry '" + port_ref + "' must be numeric");
            }
            ctx.initial_values[port_ref] = value.get<float>();
        }
    }

    spdlog::debug("[json_io] Parsed {} templates, {} devices, {} connections",
        ctx.templates.size(), ctx.devices.size(), ctx.connections.size());

    return ctx;
}

json port_to_json(const Port& port) {
    json j;
    switch (port.direction) {
        case bp2::Direction::Input:  j["direction"] = "In"; break;
        case bp2::Direction::InOut:  j["direction"] = "InOut"; break;
        default:                     j["direction"] = "Out"; break;
    }

    switch (port.type) {
        case PortType::V:           j["type"] = "V"; break;
        case PortType::I:           j["type"] = "I"; break;
        case PortType::Signal:      j["type"] = "Signal"; break;
        case PortType::Bool:        j["type"] = "Bool"; break;
        case PortType::RPM:         j["type"] = "RPM"; break;
        case PortType::Temperature: j["type"] = "Temperature"; break;
        case PortType::Pressure:    j["type"] = "Pressure"; break;
        case PortType::Position:    j["type"] = "Position"; break;
        case PortType::Contextual:  j["type"] = "Contextual"; break;
        case PortType::Any:         j["type"] = "Any"; break;
    }
    return j;
}

template <typename DeviceT>
json device_to_json(const DeviceT& dev) {
    json j;
    if (!dev.name.empty()) j["name"] = dev.name;
    if (!dev.template_name.empty()) j["template"] = dev.template_name;
    if (!dev.classname.empty()) j["classname"] = dev.classname;
    if (dev.priority != "med") j["priority"] = dev.priority;
    if (dev.bucket.has_value()) j["bucket"] = dev.bucket.value();
    if (dev.critical) j["critical"] = true;

    if (!dev.ports.empty()) {
        json ports;
        for (const auto& [name, port] : dev.ports) {
            ports[name] = port_to_json(port);
        }
        j["ports"] = ports;
    }

    if (!dev.params.empty()) {
        json params;
        for (const auto& [key, value] : dev.params) {
            params[key] = value;
        }
        j["params"] = params;
    }

    return j;
}

json connection_to_json(const Connection& conn) {
    json j;
    j["from"] = conn.from;
    j["to"] = conn.to;
    return j;
}

json subsystem_to_json(const SubsystemCall& sub) {
    json j;
    if (!sub.name.empty()) j["name"] = sub.name;
    j["template"] = sub.template_name;
    if (!sub.port_map.empty()) {
        json port_map;
        for (const auto& [external, internal] : sub.port_map) {
            port_map[external] = internal;
        }
        j["port_map"] = port_map;
    }
    return j;
}

json template_to_json(const SystemTemplate& tpl) {
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
        for (const auto& [external, internal] : tpl.exposed_ports) {
            exposed[external] = internal;
        }
        j["exposed_ports"] = exposed;
    }

    return j;
}

} // namespace

ParserContext parse_json(const std::string& json_text) {
    auto registry = load_component_registry();
    spdlog::info("[json_io] Loaded {} type definitions", registry.types.size());
    return parse_json_impl(json_text, registry, {});
}

ParserContext parse_json(const std::string& json_text, const std::string& library_dir) {
    auto registry = load_component_registry(library_dir);
    spdlog::info("[json_io] Loaded {} type definitions from '{}'", registry.types.size(), library_dir);
    return parse_json_impl(json_text, registry, {});
}

std::string serialize_json(const ParserContext& ctx) {
    json j;

    if (!ctx.templates.empty()) {
        json templates;
        for (const auto& [name, tpl] : ctx.templates) {
            templates[name] = template_to_json(tpl);
        }
        j["templates"] = templates;
    }

    if (!ctx.devices.empty()) {
        json devices;
        for (const auto& dev : ctx.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    if (!ctx.connections.empty()) {
        json connections;
        for (const auto& conn : ctx.connections) {
            connections.push_back(connection_to_json(conn));
        }
        j["connections"] = connections;
    }

    if (!ctx.initial_values.empty()) {
        json initial_values;
        for (const auto& [port_ref, value] : ctx.initial_values) {
            initial_values[port_ref] = value;
        }
        j["initial_values"] = initial_values;
    }

    return j.dump(2);
}
