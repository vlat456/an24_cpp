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
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

namespace jit_solver_impl {

// =====================================================================
// RawElement — alias for shared GenericRawElement
// =====================================================================

using RawElement = build_common::GenericRawElement<HydraulicElementKind>;

// =====================================================================
// Element extraction: function-pointer table
// =====================================================================

using Extractor = build_common::ElementExtractor<RawElement>;

// ---- Extractor functions ----

static void extract_pressure_source(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float pressure = build_common::read_role_param_required(dev, role, "pressure");
    float resistance = build_common::read_role_param_required(dev, role, "resistance");
    uint32_t node_pos = build_common::resolve_role_port(dev, role, "pos", pts, intern);
    uint32_t node_neg = build_common::resolve_role_port(dev, role, "neg", pts, intern);

    out.push_back({HydraulicElementKind::PressureSource,
        node_pos, node_neg, pressure, resistance,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_flow_branch(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float conductance = build_common::read_role_param_required(dev, role, "g");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "a", pts, intern);
    uint32_t node_b = build_common::resolve_role_port(dev, role, "b", pts, intern);
    out.push_back({HydraulicElementKind::FlowBranch,
        node_a, node_b, conductance, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_fixed_pressure_node(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float value = build_common::read_role_param_required(dev, role, "pressure");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "node", pts, intern);
    out.push_back({HydraulicElementKind::FixedPressureNode,
        node_a, UINT32_MAX, value, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

// ---- The extractor table ----

static const Extractor k_hydraulic_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &extract_fixed_pressure_node},
    {SolverRoleKind::PressureSource,    &extract_pressure_source},
    {SolverRoleKind::FlowBranch,        &extract_flow_branch},
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
// Phase 3: Assign HydraulicPrimitiveHandle to component variants
// =====================================================================

static void assign_hydraulic_handles(
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

    for (size_t island_idx = 0; island_idx < result.hydraulic.plan.islands.size(); ++island_idx) {
        const auto& island = result.hydraulic.plan.islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = result.devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error("Hydraulic handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }

            HydraulicPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.hydraulic_handle; }) {
                    comp.hydraulic_handle = handle;
                }
            }, *variant);
        }
    }
}

// =====================================================================
// Orchestrator: build_hydraulic_islands
// =====================================================================

void build_hydraulic_islands(
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_hydraulic_raw_elements(
        devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, HydraulicIslandPlan>(raw_elements, result.hydraulic.plan);
    assign_hydraulic_handles(raw_elements, result);
}

// =====================================================================
// Hydraulic patch ops + solver step ops
// =====================================================================

void build_hydraulic_patch_ops(BuildResult& result)
{
    result.hydraulic.patch_ops.clear();

    auto add_op = [&](const HydraulicPatchOp& op) {
        if (op.element_id == UINT32_MAX) return;
        result.hydraulic.patch_ops.push_back(op);
    };

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;

            if constexpr (std::is_same_v<T, SolenoidValve<JitProvider>>) {
                // BoolSwitch: reads committed open/closed state from signal.
                // state=true → valve open (high conductance), state=false → valve closed (low conductance).
                if (!is_valid(comp.hydraulic_handle)) return;
                HydraulicPatchOp op;
                op.kind = HydraulicPatchKind::BoolSwitch;
                op.element_id = comp.hydraulic_handle.element_id;
                op.s0 = comp.provider.get(PortNames::state);
                op.state_true_value = comp.g_open;    // valve open when state=true
                op.state_false_value = comp.g_closed;  // valve closed when state=false
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, FuelTank<JitProvider>>) {
                // CopySignal: reads gravity pressure from p_source signal,
                // writes to PressureSource element_value_a (P_th).
                // One-frame delay consistent with electrical solver-owned pattern.
                if (!is_valid(comp.hydraulic_handle)) return;
                HydraulicPatchOp op;
                op.kind = HydraulicPatchKind::CopySignal;
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
    { t.hydraulic_handle } -> std::same_as<HydraulicPrimitiveHandle&>;
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