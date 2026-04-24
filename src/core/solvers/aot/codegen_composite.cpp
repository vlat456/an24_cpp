#include "codegen_internal.h"
#include "core/registry/component_resolution.h"
#include "blueprint_v2/elaboration/codegen_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"

namespace {

/// Build a BlueprintLibrary from all composite types in the registry.
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

/// Shared pipeline: Blueprint → Flattener → elaborate_for_codegen → codegen output.
/// Both generate_composite_systems and generate_all_composites converge here.
CompositeCodegenResult flatten_and_generate(
    const CompositeSpec& td,
    const ComponentRegistry& registry,
    ui::StringInterner& interner,
    const bp2::BlueprintLibrary& library)
{
    auto bp = bp2::blueprint_from_type_definition(ComponentSpec{td}, interner, registry);
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
    result.header = CodeGen::generate_header(source_file, input.devices, {},
                                              input.port_to_signal, input.signal_count, class_name, electrical_plan);
    result.source = CodeGen::generate_source(header_name, input.devices, {},
                                              input.port_to_signal, input.signal_count, class_name, electrical_plan);
    return result;
}

} // namespace

CompositeCodegenResult CodeGen::generate_composite_systems(
    const CompositeSpec& td,
    const ComponentRegistry& registry)
{
    ui::StringInterner interner;
    auto library = build_library_from_registry(registry, interner);
    return flatten_and_generate(td, registry, interner, library);
}

std::map<std::string, CompositeCodegenResult> CodeGen::generate_all_composites(const ComponentRegistry& registry) {
    ui::StringInterner interner;
    auto library = build_library_from_registry(registry, interner);

    auto order = registry.get_composites_topo_sorted();

    std::map<std::string, CompositeCodegenResult> results;
    for (const auto& name : order) {
        const auto* td = registry.get(name);
        if (td && is_composite(*td)) {
            results[name] = flatten_and_generate(*as_composite(*td), registry, interner, library);
        }
    }

    return results;
}
