/// Pneumatic domain build pipeline — element extraction, island grouping,
/// handle assignment, patch ops, and step ops.
///
/// Uses the same patterns as hydraulic: schema-registered extractors,
/// union-find island grouping, structural typing for handles, metadata-driven
/// patch ops.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for step ops only (patch ops are metadata-driven).
#include "components/pneumatic_compressor.h"
#include "components/pneumatic_valve.h"
#include "core/solvers/common/provider.h"

namespace jit_solver_impl {

using RawElement = build_common::GenericRawElement<NodalElementKind>;
using Extractor = build_common::ElementExtractor<RawElement>;

static const Extractor k_pneumatic_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &build_common::extract_fixed_pressure_node<RawElement>},
    {SolverRoleKind::PressureSource,    &build_common::extract_pressure_source<RawElement>},
    {SolverRoleKind::FlowBranch,        &build_common::extract_flow_branch<RawElement>},
};

static std::vector<RawElement> extract_pneumatic_raw_elements(
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
        if (role.domain != Domain::Pneumatic) continue;

        const auto* extractor = build_common::find_extractor(
            k_pneumatic_extractors, std::size(k_pneumatic_extractors), role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported pneumatic solver_role kind '" +
                std::string(solver_role_kind_name(role.kind)) +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }

        extractor->extract(dev, role, port_to_signal, signal_key_interner,
                           build_common::should_bind_handle(role), raw_elements, element_idx);
    }

    return raw_elements;
}

void build_pneumatic_islands(
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_pneumatic_raw_elements(
        devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, NodalIslandPlan>(raw_elements, result.pneumatic.plan);
    build_common::assign_single_handles(
        raw_elements, result.pneumatic.plan.islands, result.devices,
        [](NodalPrimitiveHandle handle, ComponentVariant& variant) {
            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.pneumatic_handle; }) {
                    comp.pneumatic_handle = handle;
                }
            }, variant);
        }, "Pneumatic");

    // Data-driven patch ops from solver_role.patch_op metadata.
    auto element_id_map = build_common::build_element_id_map(raw_elements);
    build_common::build_patch_ops_from_metadata(
        result.pneumatic.patch_ops, devices, element_id_map,
        result.port_to_signal, result.signal_key_interner);
}

// =====================================================================
// Pneumatic step ops (structural typing)
// =====================================================================

template<typename T>
constexpr bool is_pneumatic_solver_owned_v = requires(T t) {
    { t.pneumatic_handle } -> std::same_as<NodalPrimitiveHandle&>;
};

void build_pneumatic_step_ops(BuildResult& result)
{
    result.pneumatic.execute_ops.clear();
    result.pneumatic.commit_ops.clear();

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (is_pneumatic_solver_owned_v<T>) {
                result.pneumatic.execute_ops.push_back(
                    {&comp, &execute_component_adapter<T>});
                result.pneumatic.commit_ops.push_back(
                    {&comp, &commit_component_adapter<T>});
            }
        }, variant);
    });
}

}  // namespace jit_solver_impl
