#pragma once

/// @file elaboration_detail.h
/// Shared device-building logic used by both elaborate_for_jit() and
/// elaborate_for_codegen(). Internal — not a public API.

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/elaboration/elaboration_utils.h"
#include "core/model/component_registry.h"
#include "core/model/component_kind.h"
#include "core/model/resolved_device.h"
#include "core/solvers/common/signal_key.h"
#include "core/strings/interned_id.h"

#include <set>
#include <string>
#include <vector>

namespace bp2::elaboration::detail {

// =====================================================================
// build_resolved_device — per-component ResolvedDevice builder
//
// Shared by both elaborate_for_jit() and elaborate_for_codegen().
// Must come before collect_devices() which calls it.
// =====================================================================

/// Build a ResolvedDevice from a FlatNetlist component.
///
/// @param comp       The flattened component
/// @param dev_id     Colon-separated node ID (from node_id_from_path)
/// @param classname  Resolved classname string (avoids redundant interner.resolve)
/// @param type_def   ComponentSpec from registry
/// @param interner   String interner for port name resolution
/// @param registry   Type registry (for presentation metadata)
/// @param fill_defaults  If true, fills default params from spec (needed by codegen)
/// @return The resolved device, or std::nullopt if it should be skipped (visual-only)
inline std::optional<ResolvedDevice> build_resolved_device(
    const FlatNetlist::Component& comp,
    const std::string& dev_id,
    const std::string& classname,
    const ComponentSpec& type_def,
    const core::StringInterner& interner,
    const ComponentRegistry& registry,
    bool fill_defaults)
{

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

// =====================================================================
// collect_devices — Phase 1: build device list from FlatNetlist
// =====================================================================

/// Result of Phase 1 device collection — shared between JIT and codegen paths.
struct CollectedDevices {
    std::vector<ResolvedDevice> devices;
};

/// Phase 1: Collect simulation-relevant devices from a FlatNetlist.
///
/// Skips bridge nodes, visual-only types, and deduplicates by dev_id.
/// The fill_defaults flag controls whether missing params are filled from spec
/// (codegen needs full param set; JIT doesn't).
inline CollectedDevices collect_devices(
    const FlatNetlist& netlist,
    PathArena& arena,
    const core::StringInterner& interner,
    const ComponentRegistry& type_registry,
    bool fill_defaults)
{
    CollectedDevices result;
    std::set<std::string> emitted_ids;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        if (!emitted_ids.insert(dev_id).second) continue;

        // Bridge nodes are structural — not simulation devices
        if (!comp.exposed_port_name.empty()) continue;

        const std::string classname(interner.resolve(comp.type));
        const ComponentSpec* type_def = type_registry.get(classname);
        if (!type_def) {
            throw std::runtime_error("Component definition not found: " + classname);
        }

        auto dev = build_resolved_device(
            comp, dev_id, classname, *type_def, interner, type_registry,
            fill_defaults);
        if (!dev.has_value()) continue;

        result.devices.push_back(std::move(*dev));
    }

    return result;
}

// =====================================================================
// collect_port_signals — Phase 2: build port_to_signal map
// =====================================================================

/// Result of Phase 2 signal extraction — indices and max tracking.
struct SignalMapResult {
    uint32_t next_signal = 0;
};

/// Phase 2 core: iterate port_signals and bridge exposures, calling back
/// for each key-signal pair. The caller decides key representation.
///
/// @param on_key_sig  Called with (const std::string& key, uint32_t sig_idx)
///                    for every port signal and bridge exposure.
template <typename Fn>
inline SignalMapResult collect_port_signals(
    const FlatNetlist& netlist,
    PathArena& arena,
    const core::StringInterner& interner,
    Fn&& on_key_sig)
{
    SignalMapResult result;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);

        for (const auto& [port_iid, sig_idx] : comp.port_signals) {
            const std::string port_name(interner.resolve(port_iid));
            const std::string key = signal_key::make_node_port_key(dev_id, port_name);

            if (sig_idx >= result.next_signal) result.next_signal = sig_idx + 1;
            on_key_sig(key, sig_idx);
        }

        // Bridge nodes: expose the parent-facing key pointing to the ext signal
        if (!comp.exposed_port_name.empty()) {
            const std::string exposed_key = exposed_key_for_bridge(dev_id, comp.exposed_port_name, interner);
            if (!exposed_key.empty()) {
                for (const auto& [port_iid, sig_idx] : comp.port_signals) {
                    if (interner.resolve(port_iid) == "ext") {
                        if (sig_idx >= result.next_signal) result.next_signal = sig_idx + 1;
                        on_key_sig(exposed_key, sig_idx);
                        break;
                    }
                }
            }
        }
    }

    return result;
}

} // namespace bp2::elaboration::detail
