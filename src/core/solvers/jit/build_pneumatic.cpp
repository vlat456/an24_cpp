/// Pneumatic domain build pipeline — element extraction, island grouping,
/// handle assignment, patch ops, and step ops.
///
/// Uses the same architectural patterns as hydraulic: schema-registered
/// extractors, union-find island grouping, structural typing for handles.
/// The unified nodal subsolver handles the actual solve — this file only
/// produces NodalElement structs with NodalElementKind values.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for visit-based build ops.
#include "components/pneumatic_compressor.h"
#include "components/pneumatic_valve.h"
#include "core/solvers/common/provider.h"
#include <unordered_set>

namespace jit_solver_impl {

// =====================================================================
// RawElement — alias for shared GenericRawElement
// =====================================================================

using RawElement = build_common::GenericRawElement<NodalElementKind>;

// =====================================================================
// Element extraction: function-pointer table
// =====================================================================

using Extractor = build_common::ElementExtractor<RawElement>;

// ---- The extractor table (shared pressure-domain extractors) ----

static const Extractor k_pneumatic_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &build_common::extract_fixed_pressure_node<RawElement>},
    {SolverRoleKind::PressureSource,    &build_common::extract_pressure_source<RawElement>},
    {SolverRoleKind::FlowBranch,        &build_common::extract_flow_branch<RawElement>},
};

// =====================================================================
// Phase 1: Extract raw pneumatic elements from solver_role devices
// =====================================================================

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

        // Only process pneumatic domain solver_roles.
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

// =====================================================================
// Phase 3: Assign NodalPrimitiveHandle to component variants
// =====================================================================

static void assign_pneumatic_handles(
    const std::vector<RawElement>& raw_elements,
    BuildResult& result)
{
    std::unordered_map<uint32_t, std::string> element_id_to_device;
    element_id_to_device.reserve(raw_elements.size());
    for (const auto& raw_elem : raw_elements) {
        if (!raw_elem.device_name.empty()) {
            element_id_to_device[static_cast<uint32_t>(raw_elem.element_id)] = raw_elem.device_name;
        }
    }

    for (size_t island_idx = 0; island_idx < result.pneumatic.plan.islands.size(); ++island_idx) {
        const auto& island = result.pneumatic.plan.islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = result.devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error("Pneumatic handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }

            NodalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.pneumatic_handle; }) {
                    comp.pneumatic_handle = handle;
                }
            }, *variant);
        }
    }
}

// =====================================================================
// Orchestrator: build_pneumatic_islands
// =====================================================================

void build_pneumatic_islands(
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_pneumatic_raw_elements(
        devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, NodalIslandPlan>(raw_elements, result.pneumatic.plan);
    assign_pneumatic_handles(raw_elements, result);
}

// =====================================================================
// Pneumatic patch ops + solver step ops
// =====================================================================

void build_pneumatic_patch_ops(BuildResult& result)
{
    result.pneumatic.patch_ops.clear();

    auto add_op = [&](const NodalPatchOp& op) {
        if (op.element_id == UINT32_MAX) return;
        result.pneumatic.patch_ops.push_back(op);
    };

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;

            if constexpr (std::is_same_v<T, PneumaticValve<JitProvider>>) {
                if (!is_valid(comp.pneumatic_handle)) return;
                NodalPatchOp op;
                op.kind = NodalPatchKind::BoolSwitch;
                op.element_id = comp.pneumatic_handle.element_id;
                op.s0 = comp.provider.get(PortNames::state);
                op.state_true_value = comp.g_open;
                op.state_false_value = comp.g_closed;
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, PneumaticCompressor<JitProvider>>) {
                if (!is_valid(comp.pneumatic_handle)) return;
                NodalPatchOp op;
                op.kind = NodalPatchKind::CopySignal;
                op.element_id = comp.pneumatic_handle.element_id;
                op.s0 = comp.provider.get(PortNames::p_source);
                add_op(op);
            }
        }, variant);
    });
}

// Trait: true for component types that participate in the pneumatic solver's
// per-frame execute/commit cycle.
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
