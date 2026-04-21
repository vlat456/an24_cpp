#include "codegen_composite_helpers.h"
#include "core/registry/component_resolution.h"
#include "core/registry/composite_expansion.h"

#include <set>

CompositeCodegenResult CodeGen::generate_composite_systems(
    const CompositeSpec& td,
    const ComponentRegistry& registry)
{
    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(td, registry, loading_stack);

    std::vector<ResolvedDevice> resolved_devices;
    resolved_devices.reserve(expanded.devices.size());
    for (const auto& dev : expanded.devices) {
        const auto* type_def = registry.get(dev.classname);
        if (!type_def) {
            throw std::runtime_error("Missing component definition for '" + dev.classname + "'");
        }
        // Skip visual-only types — they don't participate in simulation
        if (const auto* pres = registry.presentation.get(dev.classname); pres && pres->visual_only) {
            continue;
        }
        resolved_devices.push_back(resolve_component(dev, *type_def));
    }

    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    codegen_composite_detail::build_port_index_map(resolved_devices, expanded.bridge_ports, all_ports, port_to_idx);

    codegen_composite_detail::UnionFind uf(all_ports.size());
    codegen_composite_detail::apply_signal_allocation_rules(uf, resolved_devices, expanded.bridge_ports, expanded.connections, port_to_idx);

    uint32_t signal_count = 0;
    auto port_to_signal = codegen_composite_detail::finalize_signal_indices(uf, all_ports, port_to_idx, signal_count);

    ElectricalPlanCodegen electrical_plan = extract_electrical_plan(resolved_devices, port_to_signal);

    std::string class_name = codegen_detail::sanitize_name(td.classname) + "_Systems";
    std::string source_file = td.classname + ".blueprint";
    std::string header_name = "generated_" + codegen_detail::sanitize_name(td.classname) + ".h";

    CompositeCodegenResult result;
    result.class_name = class_name;
    result.header = generate_header(source_file, resolved_devices, expanded.connections,
                                    port_to_signal, signal_count, class_name, electrical_plan);
    result.source = generate_source(header_name, resolved_devices, expanded.connections,
                                    port_to_signal, signal_count, class_name, electrical_plan);
    return result;
}

std::map<std::string, CompositeCodegenResult> CodeGen::generate_all_composites(const ComponentRegistry& registry) {
    std::map<std::string, CompositeCodegenResult> results;

    auto order = registry.get_composites_topo_sorted();

    for (const auto& name : order) {
        const auto* td = registry.get(name);
        if (td && is_composite(*td)) {
            results[name] = generate_composite_systems(*as_composite(*td), registry);
        }
    }

    return results;
}
