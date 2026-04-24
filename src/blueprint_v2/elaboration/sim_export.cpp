#include "sim_export.h"

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/solvers/common/signal_key.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/model/component_kind.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp2::elaboration {

std::string node_id_from_path(Path node_path, PathArena& arena, const ui::StringInterner& interner) {
    std::vector<std::string> segments;
    Path cur = node_path;
    while (cur.kind() != PathKind::Root) {
        if (cur.kind() == PathKind::Nested || cur.kind() == PathKind::Node) {
            segments.emplace_back(interner.resolve(cur.segment()));
        }
        cur = arena.parent(cur);
    }

    std::string out;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (!out.empty()) out.push_back(':');
        out += *it;
    }
    return out;
}

namespace {

std::string exposed_key_for_component(const FlatNetlist::Component& comp,
                                      std::string_view dev_id,
                                      const ui::StringInterner& interner) {
    const size_t sep = dev_id.rfind(':');
    if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= dev_id.size()) {
        return "";
    }

    const std::string_view parent_instance = dev_id.substr(0, sep);
    if (!comp.exposed_port_name.empty()) {
        return signal_key::make_node_port_key(parent_instance, interner.resolve(comp.exposed_port_name));
    }

    const std::string_view bridge_segment = dev_id.substr(sep + 1);
    return signal_key::make_node_port_key(parent_instance, bridge_segment);
}

} // namespace

// ==================================================================
// elaborate_for_jit — direct FlatNetlist → JitBuildInput (no JSON)
// ==================================================================

JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry) {

    JitBuildInput result;

    // --- Convert flat components to canonical resolved runtime devices ---
    std::set<std::string> emitted_ids;
    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        if (!emitted_ids.insert(dev_id).second) {
            continue;
        }

        if (!comp.exposed_port_name.empty()) {
            continue;
        }

        const std::string classname(interner.resolve(comp.type));

        const ComponentSpec* type_def = type_registry.get(classname);
        if (!type_def) {
            throw std::runtime_error("Component definition not found: " + classname);
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
        auto is_visual_only = [&](const std::string& key) -> bool {
            const auto& params = spec_params(*type_def);
            auto it = params.find(key);
            return it != params.end() && it->second.visual_only;
        };
        auto is_int_param = [&](const std::string& key) -> bool {
            const auto& params = spec_params(*type_def);
            auto it = params.find(key);
            return it != params.end() && it->second.type == ParamSchemaType::Int;
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

        const auto& domains = spec_domains(*type_def);
        if (domains.empty()) {
            throw std::runtime_error(
                "Missing domains metadata in component spec for component '" + spec_classname(*type_def) + "'");
        }

        dev.display_name = classname;

        // Filter out visual-only devices at the elaboration boundary
        if (auto* pres = type_registry.presentation.get(classname)) {
            if (!pres->description.empty()) {
                dev.display_name = pres->description;
            }
            if (pres->visual_only) {
                continue;
            }
        }
        const auto& meta = spec_meta(*type_def);
        dev.domains = domains;
        dev.execution = spec_execution(*type_def);
        dev.solver_role = spec_solver_role(*type_def);
        dev.priority = meta.priority;
        dev.critical = meta.critical;

        // scheduler_source and solver_owned_electrical come from PrimitiveSpec.solver
        if (const PrimitiveSpec* prim = as_primitive(*type_def)) {
            dev.scheduler_source = prim->solver.scheduler_source;
            dev.solver_owned_electrical = prim->solver.solver_owned_electrical;
        }

        result.devices.push_back(std::move(dev));
    }

    // --- Build port_to_signal directly from FlatNetlist signal indices ---
    // compact_signals() already produces dense contiguous indices,
    // so signal indices are used directly — no remap needed.
    //
    // Keys are interned via signal_key_interner — string construction happens
    // once at build time, runtime lookups use InternedId (integer comparison).
    uint32_t next_signal = 0;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        for (const auto& [port_iid, sig_idx] : comp.port_signals) {
            const std::string port_name(interner.resolve(port_iid));
            const std::string key = signal_key::make_node_port_key(dev_id, port_name);

            // Track the maximum signal index to compute signal_count
            if (sig_idx >= next_signal) next_signal = sig_idx + 1;
            result.port_to_signal[result.signal_key_interner.intern(key)] = sig_idx;
        }

        // Structural bridge nodes are lowered away as runtime devices; only
        // their exposed parent interface key remains in the signal map.
        if (!comp.exposed_port_name.empty()) {
            const std::string exposed_key = exposed_key_for_component(comp, dev_id, interner);
            if (!exposed_key.empty()) {
                for (const auto& [port_iid, sig_idx] : comp.port_signals) {
                    const std::string pn(interner.resolve(port_iid));
                    if (pn == "ext") {
                        if (sig_idx >= next_signal) next_signal = sig_idx + 1;
                        result.port_to_signal[result.signal_key_interner.intern(exposed_key)] = sig_idx;
                        break;
                    }
                }
            }
        }
    }

    result.signal_count = next_signal + 1; // +1 for sentinel

    return result;
}

} // namespace bp2::elaboration
