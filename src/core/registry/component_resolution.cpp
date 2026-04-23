#include "core/registry/component_resolution.h"
#include "core/model/presentation_spec.h"
#include "core/model/component_kind.h"
#include "parse_number.h"

#include <spdlog/spdlog.h>

namespace {

std::string port_type_to_string(PortType type) {
    switch (type) {
        case PortType::V:           return "V";
        case PortType::I:           return "I";
        case PortType::Signal:      return "Signal";
        case PortType::Bool:        return "Bool";
        case PortType::RPM:         return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure:    return "Pressure";
        case PortType::Position:    return "Position";
        case PortType::Contextual:  return "Contextual";
        case PortType::Any:         return "Any";
    }
    return "Unknown";
}

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSpec>& schema,
    const std::string& dev_name,
    const std::string& classname)
{
    for (const auto& [name, spec] : schema) {
        auto it = params.find(name);
        if (it == params.end()) {
            if (spec.required) {
                throw std::runtime_error("Missing required parameter '" + name + "' on device '" + dev_name + "' (" + classname + ")");
            }
            continue;
        }

        const std::string& value = it->second;
        switch (spec.type) {
            case ParamSchemaType::Float: {
                float parsed = 0.0f;
                if (!locale_safe::parse_float(value, parsed)) {
                    throw std::runtime_error("Parameter '" + name + "' must be float on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.min.has_value() && static_cast<double>(parsed) < *spec.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.max.has_value() && static_cast<double>(parsed) > *spec.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Int: {
                long long parsed = 0;
                if (!locale_safe::parse_int64(value, parsed)) {
                    throw std::runtime_error("Parameter '" + name + "' must be int on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.min.has_value() && static_cast<double>(parsed) < *spec.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.max.has_value() && static_cast<double>(parsed) > *spec.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Bool:
                if (!(value == "true" || value == "false" || value == "1" || value == "0")) {
                    throw std::runtime_error("Parameter '" + name + "' must be bool on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            case ParamSchemaType::String:
            case ParamSchemaType::Table:
                break;
        }
    }
}

} // namespace

ResolvedDevice resolve_component(
    const DeviceInstance& instance,
    const ComponentSpec& definition)
{
    DeviceInstance merged = instance;

    const auto& ports = spec_ports(definition);
    if (merged.ports.empty()) {
        merged.ports = ports;
    } else {
        for (const auto& [port_name, port] : ports) {
            if (!merged.ports.count(port_name)) {
                merged.ports[port_name] = port;
            } else {
                merged.ports[port_name].type = port.type;
                merged.ports[port_name].alias = port.alias;
                merged.ports[port_name].domain = port.domain;
                merged.ports[port_name].source_writer = port.source_writer;
            }
        }
    }

    const auto& params = spec_params(definition);
    for (const auto& [param_name, param_spec] : params) {
        if (param_spec.visual_only) {
            continue;
        }
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_spec.default_value;
        }
    }

    for (auto it = merged.params.begin(); it != merged.params.end(); ) {
        auto spec_it = params.find(it->first);
        if (spec_it != params.end() && spec_it->second.visual_only) {
            it = merged.params.erase(it);
        } else {
            ++it;
        }
    }

    const auto& domains = spec_domains(definition);
    if (domains.empty()) {
        throw std::runtime_error(
            "Missing domains metadata in component spec for component '" + spec_classname(definition) + "'");
    }

    const auto& meta = spec_meta(definition);
    if (merged.priority == "med" && meta.priority != "med") {
        merged.priority = meta.priority;
    }
    if (!merged.critical && meta.critical) {
        merged.critical = true;
    }

    if (!params.empty()) {
        validate_params_against_schema(merged.params, params, merged.name, merged.classname);
    }

    ResolvedDevice resolved;
    resolved.name = merged.name;
    resolved.template_name = merged.template_name;
    resolved.classname = merged.classname;
    resolved.display_name = merged.display_name;
    resolved.priority = merged.priority;
    resolved.bucket = merged.bucket;
    resolved.critical = merged.critical;
    resolved.ports = std::move(merged.ports);
    resolved.params = std::move(merged.params);
    resolved.pos = merged.pos;
    resolved.size = merged.size;
    resolved.kind = parse_component_kind(merged.classname).value_or(ComponentKind::_COUNT);
    // scheduler_source and solver_owned_electrical come from PrimitiveSpec.solver
    resolved.domains = spec_domains(definition);

    if (const PrimitiveSpec* prim = as_primitive(definition)) {
        resolved.scheduler_source = prim->solver.scheduler_source;
        resolved.solver_owned_electrical = prim->solver.solver_owned_electrical;
        resolved.execution = prim->solver.execution;
        resolved.solver_role = prim->solver.solver_role;
    }

    return resolved;
}

std::unordered_map<std::string, Port> extract_exposed_ports(const ComponentSpec& spec) {
    const auto* comp = as_composite(spec);
    if (!comp) {
        return {};
    }

    std::unordered_map<std::string, Port> exposed;
    for (const auto& bridge : comp->bridge_ports) {
        Port port;
        port.direction = bp2::to_port_direction(bridge.direction);
        port.type = bridge.type;
        port.domain = domain_for_port_type(bridge.type);
        port.alias = std::nullopt;
        exposed[bridge.exposed_port] = port;

        spdlog::debug("[component_resolution] Exposed port: {} ({}, {})",
            bridge.exposed_port,
            port.direction == bp2::Direction::Input ? "In" : "Out",
            port_type_to_string(port.type));
    }

    return exposed;
}
