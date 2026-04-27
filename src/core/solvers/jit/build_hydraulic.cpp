/// Hydraulic domain build pipeline — element extraction, island grouping,
/// handle assignment, patch ops, and step ops.
///
/// Patch ops are built from solver_role.patch_op metadata — zero component
/// visitation. Adding a new hydraulic component requires only a blueprint
/// JSON change.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for step ops only (patch ops are metadata-driven).
#include "components/fuel_tank.h"
#include "components/solenoid_valve.h"
#include "core/solvers/common/provider.h"

namespace jit_solver_impl {

using RawElement = build_common::GenericRawElement<NodalElementKind>;
using Extractor = build_common::ElementExtractor<RawElement>;

static const Extractor k_hydraulic_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &build_common::extract_fixed_pressure_node<RawElement>},
    {SolverRoleKind::PressureSource,    &build_common::extract_pressure_source<RawElement>},
    {SolverRoleKind::FlowBranch,        &build_common::extract_flow_branch<RawElement>},
};

static std::vector<RawElement> extract_hydraulic_raw_elements(
    const std::vector<SolverDevice>& devices,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    std::vector<RawElement> raw_elements;
    raw_elements.reserve(devices.size());

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;
        if (role.domain != Domain::Hydraulic) continue;

        const auto* extractor = build_common::find_extractor(
            k_hydraulic_extractors, std::size(k_hydraulic_extractors), role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported hydraulic solver_role kind '" +
                std::string(solver_role_kind_name(role.kind)) +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }

        extractor->extract(dev, role, port_to_signal, signal_key_interner,
                           build_common::should_bind_handle(role), raw_elements, element_idx);
    }

    return raw_elements;
}

void build_hydraulic_islands(
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_hydraulic_raw_elements(
        devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, NodalIslandPlan>(raw_elements, result.hydraulic.plan);
    build_common::assign_single_handles(
        raw_elements, result.hydraulic.plan.islands, result.devices,
        [](NodalPrimitiveHandle handle, ComponentVariant& variant) {
            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.hydraulic_handle; }) {
                    comp.hydraulic_handle = handle;
                }
            }, variant);
        }, "Hydraulic");

    // Data-driven patch ops from solver_role.patch_op metadata.
    auto element_id_map = build_common::build_element_id_map(raw_elements);
    build_common::build_patch_ops_from_metadata(
        result.hydraulic.patch_ops, devices, element_id_map,
        result.port_to_signal, result.signal_key_interner);
}

// =====================================================================
// Hydraulic step ops (structural typing)
// =====================================================================

template<typename T>
constexpr bool is_hydraulic_solver_owned_v = requires(T t) {
    { t.hydraulic_handle } -> std::same_as<NodalPrimitiveHandle&>;
};

void build_hydraulic_step_ops(BuildResult& result)
{
    result.hydraulic.execute_ops.clear();
    result.hydraulic.commit_ops.clear();

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (is_hydraulic_solver_owned_v<T>) {
                result.hydraulic.execute_ops.push_back(
                    {&comp, &execute_component_adapter<T>});
                result.hydraulic.commit_ops.push_back(
                    {&comp, &commit_component_adapter<T>});
            }
        }, variant);
    });
}

}  // namespace jit_solver_impl
