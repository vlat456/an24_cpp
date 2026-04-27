/// Hydraulic domain build pipeline — element extraction, island grouping,
/// handle assignment, patch ops, and step ops.
///
/// Mirrors build_electrical.cpp for the hydraulic domain.
/// Uses the same architectural patterns: schema-registered extractors,
/// union-find island grouping, structural typing for handle assignment.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for visit-based build ops.
#include "components/fuel_tank.h"
#include "components/solenoid_valve.h"
#include "core/solvers/common/provider.h"

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

static const Extractor k_hydraulic_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &build_common::extract_fixed_pressure_node<RawElement>},
    {SolverRoleKind::PressureSource,    &build_common::extract_pressure_source<RawElement>},
    {SolverRoleKind::FlowBranch,        &build_common::extract_flow_branch<RawElement>},
};

// =====================================================================
// Phase 1: Extract raw hydraulic elements from solver_role devices
// =====================================================================

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

        // Only process hydraulic domain solver_roles.
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

// =====================================================================
// Phase 3: Assign NodalPrimitiveHandle to component variants
//          (delegates to build_common::assign_single_handles)
// =====================================================================

// =====================================================================
// Orchestrator: build_hydraulic_islands
// =====================================================================

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
}

// =====================================================================
// Hydraulic patch ops + solver step ops
// =====================================================================

void build_hydraulic_patch_ops(BuildResult& result)
{
    result.hydraulic.patch_ops.clear();

    auto add_op = [&](const NodalPatchOp& op) {
        if (op.element_id == UINT32_MAX) return;
        result.hydraulic.patch_ops.push_back(op);
    };

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;

            if constexpr (std::is_same_v<T, SolenoidValve<JitProvider>>) {
                if (!is_valid(comp.hydraulic_handle)) return;
                NodalPatchOp op;
                op.kind = NodalPatchKind::BoolSwitch;
                op.element_id = comp.hydraulic_handle.element_id;
                op.s0 = comp.provider.get(PortNames::state);
                op.state_true_value = comp.g_open;    // valve open when state=true
                op.state_false_value = comp.g_closed;  // valve closed when state=false
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, FuelTank<JitProvider>>) {
                if (!is_valid(comp.hydraulic_handle)) return;
                NodalPatchOp op;
                op.kind = NodalPatchKind::CopySignal;
                op.element_id = comp.hydraulic_handle.element_id;
                op.s0 = comp.provider.get(PortNames::p_source);
                add_op(op);
            }
        }, variant);
    });
}

// Trait: true for component types that participate in the hydraulic solver's
// per-frame execute/commit cycle.
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