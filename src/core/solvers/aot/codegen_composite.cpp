#include "codegen_internal.h"
#include "core/registry/component_resolution.h"
#include "blueprint_v2/elaboration/codegen_export.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"

namespace {

/// Build a BlueprintLibrary from all composite types in the registry.
/// Each CompositeSpec is converted to a Blueprint via blueprint_from_type_definition().
bp2::BlueprintLibrary build_library_from_registry(
    const ComponentRegistry& registry,
    ui::StringInterner& interner)
{
    bp2::BlueprintLibrary library;
    for (const auto& [name, spec] : registry.all_types()) {
        if (!is_composite(spec)) continue;
        auto bp = bp2::blueprint_from_type_definition(spec, interner, registry);
        library.add(interner.intern(name), std::move(bp));
    }
    return library;
}

} // namespace

CompositeCodegenResult CodeGen::generate_composite_systems(
    const CompositeSpec& td,
    const ComponentRegistry& registry)
{
    // Convert CompositeSpec to Blueprint, flatten, elaborate for codegen.
    // This is the unified path: Flattener provides expansion + signal allocation.
    ui::StringInterner interner;
    auto bp = bp2::blueprint_from_type_definition(
        // Need to wrap CompositeSpec into a ComponentSpec for the function
        ComponentSpec{td}, interner, registry);

    auto library = build_library_from_registry(registry, interner);

    bp2::PathArena arena(interner);
    bp2::Flattener flattener(library);
    auto netlist = flattener.flatten(bp, arena);

    auto input = bp2::elaboration::elaborate_for_codegen(netlist, arena, interner, registry);

    ElectricalPlanCodegen electrical_plan = extract_electrical_plan(input.devices, input.port_to_signal);

    std::string class_name = codegen_detail::sanitize_name(td.classname) + "_Systems";
    std::string source_file = td.classname + ".blueprint";
    std::string header_name = "generated_" + codegen_detail::sanitize_name(td.classname) + ".h";

    CompositeCodegenResult result;
    result.class_name = class_name;
    // Connections parameter is unused by generate_header/generate_source ((void)connections)
    result.header = generate_header(source_file, input.devices, {},
                                     input.port_to_signal, input.signal_count, class_name, electrical_plan);
    result.source = generate_source(header_name, input.devices, {},
                                     input.port_to_signal, input.signal_count, class_name, electrical_plan);
    return result;
}

std::map<std::string, CompositeCodegenResult> CodeGen::generate_all_composites(const ComponentRegistry& registry) {
    // Build library once, reuse for all composites.
    ui::StringInterner interner;
    auto library = build_library_from_registry(registry, interner);

    auto order = registry.get_composites_topo_sorted();

    std::map<std::string, CompositeCodegenResult> results;
    for (const auto& name : order) {
        const auto* td = registry.get(name);
        if (td && is_composite(*td)) {
            const auto& comp = *as_composite(*td);

            auto bp = bp2::blueprint_from_type_definition(ComponentSpec{comp}, interner, registry);
            bp2::PathArena arena(interner);
            bp2::Flattener flattener(library);
            auto netlist = flattener.flatten(bp, arena);

            auto input = bp2::elaboration::elaborate_for_codegen(netlist, arena, interner, registry);

            ElectricalPlanCodegen electrical_plan = extract_electrical_plan(input.devices, input.port_to_signal);

            std::string class_name = codegen_detail::sanitize_name(comp.classname) + "_Systems";
            std::string source_file = comp.classname + ".blueprint";
            std::string header_name = "generated_" + codegen_detail::sanitize_name(comp.classname) + ".h";

            CompositeCodegenResult result;
            result.class_name = class_name;
            result.header = generate_header(source_file, input.devices, {},
                                             input.port_to_signal, input.signal_count, class_name, electrical_plan);
            result.source = generate_source(header_name, input.devices, {},
                                             input.port_to_signal, input.signal_count, class_name, electrical_plan);
            results[name] = std::move(result);
        }
    }

    return results;
}
