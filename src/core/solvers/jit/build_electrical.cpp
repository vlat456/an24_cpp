#include "jit_solver_internal.h"
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
#include <queue>
#include <set>
#include <unordered_set>
#include "../../../parse_number.h"

namespace jit_solver_impl {

// =====================================================================
// Adapter functions for solver step dispatch
// =====================================================================

template <typename Comp>
void commit_component_adapter(void* instance, SimulationState& st, double dt) {
    static_cast<Comp*>(instance)->commit(st, dt);
}

template <typename Comp>
void execute_component_adapter(void* instance, SimulationState& st, double dt) {
    static_cast<Comp*>(instance)->execute(st, dt);
}

// =====================================================================
// RawElement — intermediate element between device extraction and
// island grouping. Carries the device_name for handle assignment.
// =====================================================================

struct RawElement {
    ElectricalElementKind kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    size_t element_id;
    std::string device_name;  // Non-empty when bind_handle is requested
};

// =====================================================================
// Shared helpers for element extraction
// =====================================================================

/// Resolve a port name to signal index with fail-fast on missing mapping.
static uint32_t resolve_port(
    const ResolvedDevice& dev,
    const std::string& port_name,
    const PortToSignal& port_to_signal,
    const ui::StringInterner& signal_key_interner)
{
    const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
    const ui::InternedId key = signal_key_interner.lookup(full_port);
    if (key.empty()) {
        throw std::runtime_error("Port '" + full_port +
            "' not interned for component '" + dev.name + "' (classname: " + dev.classname +
            ") — signal key was never registered during allocation");
    }
    auto it = port_to_signal.find(key);
    if (it == port_to_signal.end()) {
        throw std::runtime_error("Interned port '" + full_port +
            "' has no signal mapping for component '" + dev.name + "' (classname: " + dev.classname +
            ") — interner/port_to_signal desync");
    }
    return it->second;
}

/// Read a required float param from device, parsing from string.
static float read_param_float_required(
    const ResolvedDevice& dev,
    const std::string& param_key,
    const std::string& role_key)
{
    auto it = dev.params.find(param_key);
    if (it == dev.params.end()) {
        throw std::runtime_error(
            "solver_role references missing param '" + param_key + "' via key '" + role_key +
            "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
    }
    return locale_safe::parse_float_or(it->second, 0.0f);
}

/// Resolve a solver_role port key to signal index.
static uint32_t resolve_role_port(
    const ResolvedDevice& dev,
    const SolverRole& role,
    const std::string& role_key,
    const PortToSignal& port_to_signal,
    const ui::StringInterner& signal_key_interner)
{
    auto it = role.port_map.find(role_key);
    if (it == role.port_map.end()) {
        throw std::runtime_error("solver_role missing required port key '" + role_key +
            "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
    }
    return resolve_port(dev, it->second, port_to_signal, signal_key_interner);
}

/// Read a solver_role param by role key.
/// Resolution order: param_map → dev.params, then value_map literal.
static float read_role_param_required(
    const ResolvedDevice& dev,
    const SolverRole& role,
    const std::string& role_key)
{
    auto it = role.param_map.find(role_key);
    if (it != role.param_map.end()) {
        return read_param_float_required(dev, it->second, role_key);
    }
    auto it_val = role.value_map.find(role_key);
    if (it_val != role.value_map.end()) {
        return it_val->second;
    }
    throw std::runtime_error("solver_role missing required param key '" + role_key +
        "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
}

/// Check if bind_handle is requested for this role.
static bool should_bind_handle(const SolverRole& role) {
    auto it = role.value_map.find("bind_handle");
    return it != role.value_map.end() && it->second > 0.5f;
}

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
    const ui::StringInterner& interner,
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
    const PortToSignal& pts, const ui::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float value = read_role_param_required(dev, role, "voltage");
    uint32_t node_a = resolve_role_port(dev, role, "node", pts, intern);
    out.push_back({ElectricalElementKind::FixedVoltageNode,
        node_a, UINT32_MAX, value, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_thevenin_source(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const ui::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float voltage = read_role_param_required(dev, role, "voltage");
    float resistance = read_role_param_required(dev, role, "resistance");
    uint32_t node_pos = resolve_role_port(dev, role, "pos", pts, intern);
    uint32_t node_neg = resolve_role_port(dev, role, "neg", pts, intern);
    out.push_back({ElectricalElementKind::TheveninSource,
        node_pos, node_neg, voltage, resistance,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_conductance_branch(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const ui::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float conductance = read_role_param_required(dev, role, "g");
    uint32_t node_a = resolve_role_port(dev, role, "a", pts, intern);
    uint32_t node_b = resolve_role_port(dev, role, "b", pts, intern);
    out.push_back({ElectricalElementKind::ConductanceBranch,
        node_a, node_b, conductance, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_knob_switch_branches(
    const ResolvedDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const ui::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    int positions = static_cast<int>(read_role_param_required(dev, role, "positions"));
    positions = std::clamp(positions, 2, KnobSwitch<JitProvider>::MAX_POSITIONS);
    int initial_pos = static_cast<int>(read_role_param_required(dev, role, "initial_position"));
    initial_pos = std::clamp(initial_pos, 0, positions - 1);
    float g_open_val = read_role_param_required(dev, role, "g_open");
    float g_closed_val = read_role_param_required(dev, role, "g_closed");
    uint32_t node_wiper = resolve_role_port(dev, role, "wiper", pts, intern);

    static_assert(KnobSwitch<JitProvider>::MAX_POSITIONS <= 5,
                  "KnobSwitch terminal list supports up to 5 throws");
    static const char* terminal_names[] = {"throw1", "throw2", "throw3", "throw4", "throw5"};

    for (int i = 0; i < positions; ++i) {
        uint32_t node_t = resolve_role_port(dev, role, terminal_names[i], pts, intern);
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
    const ui::StringInterner& signal_key_interner)
{
    std::vector<RawElement> raw_elements;
    raw_elements.reserve(devices.size());

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;

        const auto* extractor = find_extractor(
            k_electrical_extractors, std::size(k_electrical_extractors), role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported solver_role kind '" + role.kind +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }

        extractor->extract(dev, role, port_to_signal, signal_key_interner,
                           should_bind_handle(role), raw_elements, element_idx);
    }

    return raw_elements;
}

// =====================================================================
// Phase 2: Group raw elements into connected islands via union-find
// =====================================================================

/// Union-find over uint32_t node indices.
struct NodeUnionFind {
    std::unordered_map<uint32_t, uint32_t> parent;
    std::unordered_map<uint32_t, uint32_t> rank;

    explicit NodeUnionFind(const std::unordered_set<uint32_t>& nodes) {
        for (uint32_t n : nodes) {
            parent[n] = n;
            rank[n] = 0;
        }
    }

    uint32_t find(uint32_t x) {
        auto it = parent.find(x);
        if (it == parent.end() || it->second == x) return x;
        it->second = find(it->second);
        return it->second;
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);
        if (ra == rb) return;
        if (rank[ra] < rank[rb]) {
            parent[ra] = rb;
        } else if (rank[ra] > rank[rb]) {
            parent[rb] = ra;
        } else {
            parent[rb] = ra;
            rank[ra]++;
        }
    }
};

/// Partition raw elements into connected islands. Each island contains elements
/// whose nodes are transitively connected. Islands are sorted by root node for determinism.
static void group_into_islands(
    const std::vector<RawElement>& raw_elements,
    ElectricalBuildPlan& plan)
{
    if (raw_elements.empty()) return;

    // Collect all unique node indices
    std::unordered_set<uint32_t> all_nodes;
    for (const auto& elem : raw_elements) {
        all_nodes.insert(elem.node_a);
        if (elem.node_b != UINT32_MAX) {
            all_nodes.insert(elem.node_b);
        }
    }

    // Union connected nodes
    NodeUnionFind uf(all_nodes);
    for (const auto& elem : raw_elements) {
        if (elem.node_b != UINT32_MAX) {
            uf.unite(elem.node_a, elem.node_b);
        }
    }

    // Group elements by island root
    std::map<uint32_t, std::vector<size_t>> island_members;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        uint32_t root = uf.find(raw_elements[i].node_a);
        island_members[root].push_back(i);
    }

    // Build sorted island plans
    std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
        island_members.begin(), island_members.end());
    std::sort(sorted_islands.begin(), sorted_islands.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [root, elem_indices] : sorted_islands) {
        (void)root;
        ElectricalIslandPlan island;

        // Collect unique signal indices
        std::set<uint32_t> island_nodes;
        for (size_t idx : elem_indices) {
            island_nodes.insert(raw_elements[idx].node_a);
            if (raw_elements[idx].node_b != UINT32_MAX) {
                island_nodes.insert(raw_elements[idx].node_b);
            }
        }
        island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

        // Build elements in original insertion order
        std::vector<size_t> sorted_indices(elem_indices.begin(), elem_indices.end());
        std::sort(sorted_indices.begin(), sorted_indices.end());
        for (size_t idx : sorted_indices) {
            const auto& re = raw_elements[idx];
            island.elements.push_back({
                re.kind, re.node_a, re.node_b,
                re.value_a, re.value_b,
                static_cast<uint32_t>(re.element_id)
            });
        }

        plan.islands.push_back(std::move(island));
    }
}

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
    for (size_t island_idx = 0; island_idx < result.electrical_plan.islands.size(); ++island_idx) {
        const auto& island = result.electrical_plan.islands[island_idx];
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
    group_into_islands(raw_elements, result.electrical_plan);
    assign_handles(raw_elements, result);
}

// =====================================================================
// Electrical patch ops + solver step ops
// =====================================================================

void build_electrical_patch_ops(BuildResult& result)
{
    result.electrical_patch_ops.clear();

    auto add_op = [&](const ElectricalPatchOp& op) {
        if (op.element_id == UINT32_MAX) return;
        result.electrical_patch_ops.push_back(op);
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
                // BoolSwitch: reads committed closed/is_pressed state from signal
                if (!is_valid(comp.electrical_handle)) return;
                ElectricalPatchOp op;
                op.kind = ElectricalPatchKind::BoolSwitch;
                op.element_id = comp.electrical_handle.element_id;
                op.s0 = comp.provider.get(PortNames::state);
                op.open_value = comp.g_open;
                op.closed_value = comp.g_closed;
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
                    op.open_value = comp.g_open;
                    op.closed_value = comp.g_closed;
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
constexpr bool is_solver_owned_v = requires(T t) {
    { t.electrical_handle } -> std::same_as<ElectricalPrimitiveHandle&>;
} || requires(T t) {
    { t.electrical_handles } -> std::same_as<ElectricalPrimitiveHandle*&>;
};

void build_solver_step_ops(BuildResult& result)
{
    result.solver_execute_ops.clear();
    result.solver_commit_ops.clear();

    // Single pass over all components — create typed execute/commit adapters
    // for solver-owned electrical components. No intermediate pointer lists.
    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (is_solver_owned_v<T>) {
                result.solver_execute_ops.push_back({&comp, &execute_component_adapter<T>});
                result.solver_commit_ops.push_back({&comp, &commit_component_adapter<T>});
            }
        }, variant);
    });
}

}  // namespace
