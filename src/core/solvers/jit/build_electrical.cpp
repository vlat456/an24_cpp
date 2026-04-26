#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
// Component headers for visit-based build ops (patch ops + step ops).
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

using RawElement = build_common::GenericRawElement<ElectricalElementKind>;

// =====================================================================
// Element extraction: function-pointer table (schema registry)
//
// Each solver_role.kind maps to an extractor function that produces
// one or more RawElement structs from a device's role metadata.
// Adding a new element kind requires only: 1 function + 1 table entry.
// =====================================================================

/// Extraction function signature.
using ExtractorFn = void(*)(
    const ResolvedDevice& dev,
    const SolverRole& role,
    const PortToSignal& port_to_signal,
    const core::StringInterner& interner,
    bool bind_handle,
    std::vector<RawElement>& out,
    size_t& element_idx);

/// One entry in the extractor table.
struct ElementExtractor {
    std::string_view kind;
    ExtractorFn extract;
};

/// Find extractor by kind string. Linear scan over small array.
static const ElementExtractor* find_extractor(
    const ElementExtractor* table, size_t count, std::string_view kind)
{
    for (size_t i = 0; i < count; ++i) {
        if (table[i].kind == kind) return &table[i];
    }
    return nullptr;
}

// ---- Extractor functions ----

static void extract_fixed_voltage_node(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float value = build_common::read_role_param_required(dev, role, "voltage");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "node", pts, intern);
    out.push_back({ElectricalElementKind::FixedVoltageNode,
        node_a, UINT32_MAX, value, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_thevenin_source(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float voltage = build_common::read_role_param_required(dev, role, "voltage");
    float resistance = build_common::read_role_param_required(dev, role, "resistance");
    uint32_t node_pos = build_common::resolve_role_port(dev, role, "pos", pts, intern);
    uint32_t node_neg = build_common::resolve_role_port(dev, role, "neg", pts, intern);
    out.push_back({ElectricalElementKind::TheveninSource,
        node_pos, node_neg, voltage, resistance,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_conductance_branch(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float conductance = build_common::read_role_param_required(dev, role, "g");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "a", pts, intern);
    uint32_t node_b = build_common::resolve_role_port(dev, role, "b", pts, intern);
    out.push_back({ElectricalElementKind::ConductanceBranch,
        node_a, node_b, conductance, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_knob_switch_branches(
    const ResolvedDevice& dev, const SolverRole& role,
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
        out.push_back({ElectricalElementKind::ConductanceBranch,
            node_wiper, node_t, initial_g, 0.0f,
            element_idx++, bind_handle ? dev.name : std::string{}});
    }
}

// ---- The extractor table ----

static const ElementExtractor k_electrical_extractors[] = {
    {"FixedVoltageNode",    &extract_fixed_voltage_node},
    {"TheveninSource",      &extract_thevenin_source},
    {"ConductanceBranch",   &extract_conductance_branch},
    {"KnobSwitchBranches",  &extract_knob_switch_branches},
};

// =====================================================================
// Phase 1: Extract raw electrical elements from solver_role devices
// =====================================================================

/// Extract raw electrical elements from all devices that have a solver_role.
/// Dispatches to registered extractor functions — no if-else chain.
static std::vector<RawElement> extract_raw_elements(
    const std::vector<ResolvedDevice>& devices,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    std::vector<RawElement> raw_elements;
    raw_elements.reserve(devices.size());

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;

        // Skip solver_roles for non-electrical domains (e.g., Hydraulic).
        // These are handled by their respective domain extractors.
        if (role.domain != Domain::Electrical) continue;

        const auto* extractor = find_extractor(
            k_electrical_extractors, std::size(k_electrical_extractors), role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported solver_role kind '" + role.kind +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }

        extractor->extract(dev, role, port_to_signal, signal_key_interner,
                           build_common::should_bind_handle(role), raw_elements, element_idx);
    }

    return raw_elements;
}

// =====================================================================
// Phase 2: Group raw elements into connected islands via union-find
// (delegates to build_common)
// =====================================================================

// =====================================================================
// Phase 3: Assign ElectricalPrimitiveHandle to component variants
// =====================================================================

/// Assign handles from island elements to their owning component variants.
/// Multi-handle components (KnobSwitch) get sequential handles; single-handle
/// components get one handle.
static void assign_handles(
    const std::vector<RawElement>& raw_elements,
    BuildResult& result)
{
    // Build O(1) lookup: element_id -> device_name
    std::unordered_map<uint32_t, std::string> element_id_to_device;
    element_id_to_device.reserve(raw_elements.size());
    for (const auto& raw_elem : raw_elements) {
        if (!raw_elem.device_name.empty()) {
            element_id_to_device[static_cast<uint32_t>(raw_elem.element_id)] = raw_elem.device_name;
        }
    }

    // Assign handles from island elements
    for (size_t island_idx = 0; island_idx < result.electrical.plan.islands.size(); ++island_idx) {
        const auto& island = result.electrical.plan.islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;  // Element without handle (e.g. Resistor)

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = result.devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error("Handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }

            ElectricalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                // Multi-handle: KnobSwitch/RotarySwitch (has electrical_handles[] + num_handles)
                if constexpr (requires { comp.electrical_handles; comp.num_handles; }) {
                    if (comp.num_handles < T::MAX_POSITIONS) {
                        comp.electrical_handles[comp.num_handles++] = handle;
                    }
                }
                // Single-handle: all other solver-owned components (has electrical_handle)
                else if constexpr (requires { comp.electrical_handle; }) {
                    comp.electrical_handle = handle;
                }
            }, *variant);
        }
    }
}

// =====================================================================
// Orchestrator: build_electrical_islands
// =====================================================================

void build_electrical_islands(
    BuildResult& result,
    const std::vector<ResolvedDevice>& devices)
{
    auto raw_elements = extract_raw_elements(devices, result.port_to_signal, result.signal_key_interner);
    build_common::group_into_islands<RawElement, ElectricalIslandPlan>(raw_elements, result.electrical.plan);
    assign_handles(raw_elements, result);
}

// =====================================================================
// Electrical patch ops + solver step ops
// =====================================================================

void build_electrical_patch_ops(BuildResult& result)
{
    result.electrical.patch_ops.clear();

    auto add_op = [&](const ElectricalPatchOp& op) {
        if (op.element_id == UINT32_MAX) return;
        result.electrical.patch_ops.push_back(op);
    };

    // Visit each component once — no typed pointer lists needed.
    // Signal indices are resolved from the component's provider; runtime
    // patch execution reads from st.values[] with zero indirection.
    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;

            if constexpr (std::is_same_v<T, ControlledVoltageSource<JitProvider>>) {
                if (!is_valid(comp.electrical_handle)) return;
                ElectricalPatchOp op;
                op.kind = ElectricalPatchKind::AffineClamp;
                op.element_id = comp.electrical_handle.element_id;
                op.s0 = comp.provider.get(PortNames::cmd);
                op.s1 = comp.provider.get(PortNames::gain);
                op.s2 = comp.provider.get(PortNames::offset);
                op.s3 = comp.provider.get(PortNames::min_v);
                op.s4 = comp.provider.get(PortNames::max_v);
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, VariableConductance<JitProvider>>) {
                if (!is_valid(comp.electrical_handle)) return;
                ElectricalPatchOp op;
                op.kind = ElectricalPatchKind::LerpClamped01;
                op.element_id = comp.electrical_handle.element_id;
                op.s0 = comp.provider.get(PortNames::cmd);
                op.s1 = comp.provider.get(PortNames::g_min);
                op.s2 = comp.provider.get(PortNames::g_max);
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, AZS<JitProvider>> ||
                               std::is_same_v<T, HoldButton<JitProvider>> ||
                               std::is_same_v<T, Relay<JitProvider>>) {
                // BoolSwitch: reads committed closed/is_pressed state from signal.
                // state=true → closed (high conductance), state=false → open (low conductance).
                if (!is_valid(comp.electrical_handle)) return;
                ElectricalPatchOp op;
                op.kind = ElectricalPatchKind::BoolSwitch;
                op.element_id = comp.electrical_handle.element_id;
                op.s0 = comp.provider.get(PortNames::state);
                op.state_true_value = comp.g_closed;
                op.state_false_value = comp.g_open;
                add_op(op);
            }
            else if constexpr (std::is_same_v<T, KnobSwitch<JitProvider>> ||
                               std::is_same_v<T, RotarySwitch1ToN<JitProvider>> ||
                               std::is_same_v<T, RotarySwitchNTo1<JitProvider>>) {
                // IndexSwitch: multi-element knob, one patch op per branch
                for (int i = 0; i < comp.num_handles; ++i) {
                    if (!is_valid(comp.electrical_handles[i])) continue;
                    ElectricalPatchOp op;
                    op.kind = ElectricalPatchKind::IndexSwitch;
                    op.element_id = comp.electrical_handles[i].element_id;
                    op.s0 = comp.provider.get(PortNames::position);
                    op.index_value = i;
                    op.state_true_value = comp.g_closed;
                    op.state_false_value = comp.g_open;
                    add_op(op);
                }
            }
        }, variant);
    });
}

// Trait: true for component types that participate in the electrical solver's
// per-frame execute/commit cycle. Structural — any component with an
// electrical_handle or electrical_handles member qualifies. New components
// with handles automatically get step ops without editing this file.
template<typename T>
constexpr bool is_electrical_solver_owned_v = requires(T t) {
    { t.electrical_handle } -> std::same_as<ElectricalPrimitiveHandle&>;
} || requires(T t) {
    { t.electrical_handles } -> std::convertible_to<ElectricalPrimitiveHandle*>;
};

void build_solver_step_ops(BuildResult& result)
{
    result.electrical.execute_ops.clear();
    result.electrical.commit_ops.clear();

    // Single pass over all components — create typed execute/commit adapters
    // for solver-owned electrical components. No intermediate pointer lists.
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