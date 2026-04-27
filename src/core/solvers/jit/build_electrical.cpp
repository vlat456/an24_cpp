/// Electrical domain build pipeline — element extraction, island grouping,
/// handle assignment, patch ops, and step ops.
///
/// Patch ops are built from solver_role.patch_op metadata — zero component
/// visitation. Adding a new solver-owned component requires only a blueprint
/// JSON change, no build pipeline edits.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for step ops only (patch ops are metadata-driven).
#include "components/controlled_voltage_source.h"
#include "components/variable_conductance.h"
#include "components/azs.h"
#include "components/hold_button.h"
#include "components/relay.h"
#include "components/knob_switch.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"
#include "components/generator.h"
#include "components/resistor.h"
#include "core/solvers/common/provider.h"
#include <algorithm>
#include <concepts>
#include <map>
#include <set>
#include <unordered_set>
#include "../../../parse_number.h"

namespace jit_solver_impl {

// =====================================================================
// RawElement — alias for shared GenericRawElement
// =====================================================================

using RawElement = build_common::GenericRawElement<NodalElementKind>;

// =====================================================================
// Element extraction: function-pointer table (schema registry)
//
// Each solver_role.kind maps to an extractor function that produces
// one or more RawElement structs from a device's role metadata.
// Adding a new element kind requires only: 1 function + 1 table entry.
// =====================================================================

using Extractor = build_common::ElementExtractor<RawElement>;

// ---- Extractor functions ----

static void extract_fixed_voltage_node(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float value = build_common::read_role_param_required(dev, role, "voltage");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "node", pts, intern);
    out.push_back({NodalElementKind::FixedNode,
        node_a, UINT32_MAX, value, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_thevenin_source(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float voltage = build_common::read_role_param_required(dev, role, "voltage");
    float resistance = build_common::read_role_param_required(dev, role, "resistance");
    uint32_t node_pos = build_common::resolve_role_port(dev, role, "pos", pts, intern);
    uint32_t node_neg = build_common::resolve_role_port(dev, role, "neg", pts, intern);
    out.push_back({NodalElementKind::Source,
        node_pos, node_neg, voltage, resistance,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_conductance_branch(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float conductance = build_common::read_role_param_required(dev, role, "g");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "a", pts, intern);
    uint32_t node_b = build_common::resolve_role_port(dev, role, "b", pts, intern);
    out.push_back({NodalElementKind::Branch,
        node_a, node_b, conductance, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_knob_switch_branches(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    int positions = static_cast<int>(build_common::read_role_param_required(dev, role, "positions"));
    positions = std::clamp(positions, 2, KnobSwitch<JitProvider>::MAX_POSITIONS);
    int initial_pos = static_cast<int>(build_common::read_role_param_required(dev, role, "initial_position"));
    initial_pos = std::clamp(initial_pos, 0, positions - 1);
    float g_open_val = build_common::read_role_param_required(dev, role, "g_open");
    float g_closed_val = build_common::read_role_param_required(dev, role, "g_closed");
    uint32_t node_wiper = build_common::resolve_role_port(dev, role, "wiper", pts, intern);

    static_assert(KnobSwitch<JitProvider>::MAX_POSITIONS <= 5,
                  "KnobSwitch terminal list supports up to 5 throws");
    static const char* terminal_names[] = {"throw1", "throw2", "throw3", "throw4", "throw5"};

    for (int i = 0; i < positions; ++i) {
        uint32_t node_t = build_common::resolve_role_port(dev, role, terminal_names[i], pts, intern);
        float initial_g = (i == initial_pos) ? g_closed_val : g_open_val;
        out.push_back({NodalElementKind::Branch,
            node_wiper, node_t, initial_g, 0.0f,
            element_idx++, bind_handle ? dev.name : std::string{}});
    }
}

// ---- The extractor table ----

static const Extractor k_electrical_extractors[] = {
    {SolverRoleKind::FixedVoltageNode,    &extract_fixed_voltage_node},
    {SolverRoleKind::TheveninSource,      &extract_thevenin_source},
    {SolverRoleKind::ConductanceBranch,   &extract_conductance_branch},
    {SolverRoleKind::KnobSwitchBranches,  &extract_knob_switch_branches},
};

// =====================================================================
// Phase 1: Extract raw electrical elements from solver_role devices
// =====================================================================

static std::vector<RawElement> extract_raw_elements(
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

        if (role.domain != Domain::Electrical) continue;

        const auto* extractor = build_common::find_extractor(
            k_electrical_extractors, std::size(k_electrical_extractors), role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported solver_role kind '" +
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

static void assign_handles(
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

    for (size_t island_idx = 0; island_idx < result.electrical.plan.islands.size(); ++island_idx) {
        const auto& island = result.electrical.plan.islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = result.devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error("Handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }

            NodalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.electrical_handles; comp.num_handles; }) {
                    if (comp.num_handles < T::MAX_POSITIONS) {
                        comp.electrical_handles[comp.num_handles++] = handle;
                    }
                }
                else if constexpr (requires { comp.electrical_handle; }) {
                    comp.electrical_handle = handle;
                }
            }, *variant);
        }
    }
}

// =====================================================================
// Orchestrator: build_electrical_islands
// Handles extraction, island grouping, handle assignment,
// AND metadata-driven patch op building in one pass.
// =====================================================================

void build_electrical_islands(
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_raw_elements(devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, NodalIslandPlan>(raw_elements, result.electrical.plan);
    assign_handles(raw_elements, result);

    // Data-driven patch ops from solver_role.patch_op metadata.
    auto element_id_map = build_common::build_element_id_map(raw_elements);
    build_common::build_patch_ops_from_metadata(
        result.electrical.patch_ops, devices, element_id_map,
        result.port_to_signal, result.signal_key_interner);
}

// =====================================================================
// Electrical step ops (structural typing — already generic)
// =====================================================================

// Trait: true for component types that participate in the electrical solver's
// per-frame execute/commit cycle. Structural — any component with an
// electrical_handle or electrical_handles member qualifies.
template<typename T>
constexpr bool is_electrical_solver_owned_v = requires(T t) {
    { t.electrical_handle } -> std::same_as<NodalPrimitiveHandle&>;
} || requires(T t) {
    { t.electrical_handles } -> std::convertible_to<NodalPrimitiveHandle*>;
};

void build_solver_step_ops(BuildResult& result)
{
    result.electrical.execute_ops.clear();
    result.electrical.commit_ops.clear();

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (is_electrical_solver_owned_v<T>) {
                result.electrical.execute_ops.push_back({&comp, &execute_component_adapter<T>});
                result.electrical.commit_ops.push_back({&comp, &commit_component_adapter<T>});
            }
        }, variant);
    });
}

}  // namespace
