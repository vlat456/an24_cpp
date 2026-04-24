#pragma once

/// @file elaboration_detail.h
/// Shared device-building logic used by both elaborate_for_jit() and
/// elaborate_for_codegen(). Internal — not a public API.

#include "blueprint_v2/flattener/flat_netlist.h"
#include "core/model/component_registry.h"
#include "core/model/component_kind.h"
#include "core/model/resolved_device.h"
#include "ui/core/interned_id.h"

#include <string>

namespace bp2::elaboration::detail {

/// Build a ResolvedDevice from a FlatNetlist component.
///
/// @param comp       The flattened component
/// @param dev_id     Colon-separated node ID (from node_id_from_path)
/// @param type_def   ComponentSpec from registry
/// @param interner   String interner for port name resolution
/// @param registry   Type registry (for presentation metadata)
/// @param fill_defaults  If true, fills default params from spec (needed by codegen)
/// @return The resolved device, or std::nullopt if it should be skipped (visual-only)
inline std::optional<ResolvedDevice> build_resolved_device(
    const FlatNetlist::Component& comp,
    const std::string& dev_id,
    const ComponentSpec& type_def,
    const ui::StringInterner& interner,
    const ComponentRegistry& registry,
    bool fill_defaults)
{
    const std::string classname(interner.resolve(comp.type));

    // Skip visual-only types — they don't participate in simulation
    if (const auto* pres = registry.get_presentation(classname)) {
        if (pres->visual_only) {
            return std::nullopt;
        }
    }

    const auto& domains = spec_domains(type_def);
    if (domains.empty()) {
        throw std::runtime_error(
            "Missing domains metadata in component spec for '" + classname + "'");
    }

    ResolvedDevice dev;
    dev.name = dev_id;
    dev.classname = classname;
    dev.kind = parse_component_kind(classname).value_or(ComponentKind::Unknown);
    dev.priority = "med";
    dev.critical = false;

    // Ports: derive from the flattened PortDescriptors
    for (const auto& pd : comp.ports) {
        const std::string port_name(interner.resolve(pd.name));
        Port port;
        port.direction = pd.direction;
        port.type = pd.port_type;
        port.domain = pd.domain;
        port.source_writer = false;
        dev.ports[port_name] = port;
    }

    // Params: convert float params to strings, filtering visual-only
    const auto& spec_params_map = spec_params(type_def);

    auto is_visual_only = [&](const std::string& key) -> bool {
        auto it = spec_params_map.find(key);
        return it != spec_params_map.end() && it->second.visual_only;
    };
    auto is_int_param = [&](const std::string& key) -> bool {
        auto it = spec_params_map.find(key);
        return it != spec_params_map.end() && it->second.type == ParamSchemaType::Int;
    };

    for (const auto& [k, v] : comp.params) {
        std::string key(interner.resolve(k));
        if (is_visual_only(key)) continue;
        if (is_int_param(key)) {
            dev.params[key] = std::to_string(static_cast<long long>(v));
        } else {
            dev.params[key] = std::to_string(v);
        }
    }
    for (const auto& [k, v] : comp.string_params) {
        if (is_visual_only(k)) continue;
        dev.params[k] = v;
    }

    // Fill defaults from type definition (codegen needs complete param set)
    if (fill_defaults) {
        for (const auto& [param_name, param_spec] : spec_params_map) {
            if (param_spec.visual_only) continue;
            if (!dev.params.count(param_name)) {
                dev.params[param_name] = param_spec.default_value;
            }
        }
    }

    dev.display_name = classname;
    if (const auto* pres = registry.get_presentation(classname)) {
        if (!pres->description.empty()) {
            dev.display_name = pres->description;
        }
    }

    const auto& meta = spec_meta(type_def);
    dev.domains = domains;
    dev.execution = spec_execution(type_def);
    dev.solver_role = spec_solver_role(type_def);
    dev.priority = meta.priority;
    dev.critical = meta.critical;

    if (const PrimitiveSpec* prim = as_primitive(type_def)) {
        dev.scheduler_source = prim->solver.scheduler_source;
        dev.solver_owned_electrical = prim->solver.solver_owned_electrical;
    }

    return dev;
}

} // namespace bp2::elaboration::detail
