#include "codegen.h"
#include "codegen_utils.h"
#include "../common/signal_key.h"
#include "parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace {

/// Maximum throw positions a KnobSwitch can have (must match component definition).
static constexpr size_t KNOB_SWITCH_MAX_POSITIONS = 5;

/// Terminal names indexed by throw index (0-based).
static constexpr const char* KNOB_SWITCH_TERMINAL_NAMES[] = {
    "throw1", "throw2", "throw3", "throw4", "throw5"
};

float parse_float_codegen(const std::string& value, float default_val) {
    if (value.empty()) {
        return default_val;
    }
    return locale_safe::parse_float_or(value, default_val);
}

float read_param_or(const ResolvedDevice& dev, const char* key, float default_val) {
    auto it = dev.params.find(key);
    if (it == dev.params.end()) {
        return default_val;
    }
    return parse_float_codegen(it->second, default_val);
}

/// Read a param through solver_role indirection: role.param_map[key] → dev.params[val] → float.
float read_role_param(const SolverRole& role, const ResolvedDevice& dev,
                      const char* key, float default_val) {
    auto it = role.param_map.find(key);
    if (it == role.param_map.end()) return default_val;
    auto it_param = dev.params.find(it->second);
    if (it_param == dev.params.end()) return default_val;
    return parse_float_codegen(it_param->second, default_val);
}

/// Resolve a port name to its signal index. Returns std::nullopt if not found
/// (throws in strict mode, warns in non-strict mode).
std::optional<uint32_t> resolve_port_optional(
    const std::string& device_name,
    const std::string& classname,
    const std::string& port_name,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options)
{
    const std::string full_port = signal_key::make_node_port_key(device_name, port_name);
    auto it = port_to_signal.find(full_port);
    if (it == port_to_signal.end()) {
        if (options.strict_port_resolution) {
            throw std::runtime_error(
                "[codegen] electrical port '" + full_port +
                "' not found in signal map for device '" + device_name +
                "' (classname: " + classname + ")"
            );
        }
        if (options.warn_on_missing_ports) {
            spdlog::warn("[codegen] electrical port '{}' not found in signal map for device '{}' (classname: {}). Element skipped from electrical plan.",
                full_port, device_name, classname);
        }
        return std::nullopt;
    }
    return it->second;
}

struct ClassnameElectricalRule {
    const char* classname;
    ElectricalElementKindCodegen kind;
    const char* port_a;
    const char* port_b;
    const char* param_a;
    float param_a_default;
    const char* param_b;
    float param_b_default;
};

constexpr ClassnameElectricalRule k_classname_rules[] = {
    {"Generator",             ElectricalElementKindCodegen::TheveninSource,   "v_out", "v_in",  "v_nominal",   28.5f, "internal_r", 0.005f},
    {"RefNode",               ElectricalElementKindCodegen::FixedVoltageNode, "v",     nullptr, "value",        0.0f,  nullptr,      0.0f},
    {"Resistor",              ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  0.1f,  nullptr,      0.0f},
    {"IndicatorLight",        ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  1.0f,  nullptr,      0.0f},
    {"CurrentSense",          ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  1000.0f,nullptr,     0.0f},
    {"ElectricalConductance", ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  0.1f,  nullptr,      0.0f},
    {"ElectricalSource",      ElectricalElementKindCodegen::TheveninSource,   "v_out", "v_in",  "voltage",      28.0f, "resistance", 0.01f},
    {"ControlledVoltageSource",ElectricalElementKindCodegen::TheveninSource,  "v_pos", "v_neg", "offset",       0.0f,  "r_internal", 0.1f},
    {"VariableConductance",   ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "g_min",        0.001f, nullptr,     0.0f},
    {"AZS",                   ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "g_open",       1e-6f,  nullptr,     0.0f},
    {"HoldButton",            ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "g_open",       1e-6f,  nullptr,     0.0f},
};

} // anonymous namespace

// ===== Section 1: Helper Types & Functions for Element Extraction =====
struct RawElement {
    ElectricalElementKindCodegen kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    size_t element_id;
    std::string device_name;   // for handle assignment back to wrapper components
    std::string device_classname;
};

// Convert electrical element kind to string representation
std::string kind_to_role(ElectricalElementKindCodegen k) {
    if (k == ElectricalElementKindCodegen::FixedVoltageNode) {
        return "FixedVoltageNode";
    }
    if (k == ElectricalElementKindCodegen::TheveninSource) {
        return "TheveninSource";
    }
    return "ConductanceBranch";
}

// ===== Section 2: Raw Element Collection (solver_role path) =====
// Extract electrical elements from device with explicit solver_role.
// Handles three element kinds: FixedVoltageNode, TheveninSource, ConductanceBranch.
// Returns N elements for KnobSwitchBranches (one per throw terminal).
std::vector<RawElement> extract_solver_role_element(
    const ResolvedDevice& dev,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options,
    size_t& element_idx
) {
    std::vector<RawElement> result;
    if (!dev.solver_role.has_value()) return result;
    const auto& role = *dev.solver_role;

    if (role.kind == SolverRoleKind::FixedVoltageNode) {
        float value = read_role_param(role, dev, "voltage", 0.0f);
        auto node = resolve_port_optional(dev.name, dev.classname, role.port_map.at("node"), port_to_signal, options);
        if (!node.has_value()) return result;
        result.push_back({ ElectricalElementKindCodegen::FixedVoltageNode,
            *node, UINT32_MAX, value, 0.0f, element_idx++, dev.name, dev.classname });
        return result;
    }

    if (role.kind == SolverRoleKind::TheveninSource) {
        float voltage = read_role_param(role, dev, "voltage", 28.0f);
        float resistance = read_role_param(role, dev, "resistance", 0.01f);
        auto pos = resolve_port_optional(dev.name, dev.classname, role.port_map.at("pos"), port_to_signal, options);
        auto neg = resolve_port_optional(dev.name, dev.classname, role.port_map.at("neg"), port_to_signal, options);
        if (!pos.has_value() || !neg.has_value()) return result;
        result.push_back({ ElectricalElementKindCodegen::TheveninSource,
            *pos, *neg, voltage, resistance, element_idx++, dev.name, dev.classname });
        return result;
    }

    if (role.kind == SolverRoleKind::ConductanceBranch) {
        float g = read_role_param(role, dev, "g", 0.1f);
        auto a = resolve_port_optional(dev.name, dev.classname, role.port_map.at("a"), port_to_signal, options);
        auto b = resolve_port_optional(dev.name, dev.classname, role.port_map.at("b"), port_to_signal, options);
        if (!a.has_value() || !b.has_value()) return result;
        result.push_back({ ElectricalElementKindCodegen::ConductanceBranch,
            *a, *b, g, 0.0f, element_idx++, dev.name, dev.classname });
        return result;
    }

    if (role.kind == SolverRoleKind::KnobSwitchBranches) {
        int positions = static_cast<int>(read_role_param(role, dev, "positions", 3.0f));
        positions = std::clamp(positions, 2, static_cast<int>(KNOB_SWITCH_MAX_POSITIONS));
        int initial_pos = static_cast<int>(read_role_param(role, dev, "initial_position", 0.0f));
        initial_pos = std::clamp(initial_pos, 0, positions - 1);
        float g_open_val = read_role_param(role, dev, "g_open", 1e-9f);
        float g_closed_val = read_role_param(role, dev, "g_closed", 0.1f);
        auto node_wiper = resolve_port_optional(dev.name, dev.classname, role.port_map.at("wiper"), port_to_signal, options);
        if (!node_wiper.has_value()) return result;
        for (int i = 0; i < positions; ++i) {
            auto node_t = resolve_port_optional(dev.name, dev.classname, role.port_map.at(KNOB_SWITCH_TERMINAL_NAMES[i]), port_to_signal, options);
            if (!node_t.has_value()) continue;
            float g = (i == initial_pos) ? g_closed_val : g_open_val;
            result.push_back({ ElectricalElementKindCodegen::ConductanceBranch,
                *node_wiper, *node_t, g, 0.0f, element_idx++, dev.name, dev.classname });
        }
        return result;
    }

    throw std::runtime_error(
        "[codegen] unsupported solver_role kind '" + std::string(solver_role_kind_name(role.kind)) +
        "' for device '" + dev.name + "' (classname: " + dev.classname + ")");
}

// ===== Section 3: Raw Element Collection (classname rule path) =====
// Extract electrical element from device using classname-based rules.
std::optional<RawElement> extract_classname_rule_element(
    const ResolvedDevice& dev,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options,
    size_t& element_idx
) {
    for (const auto& rule : k_classname_rules) {
        if (dev.classname != rule.classname) continue;

        auto node_a = resolve_port_optional(dev.name, dev.classname, rule.port_a, port_to_signal, options);
        if (!node_a.has_value()) return std::nullopt;

        if (rule.kind == ElectricalElementKindCodegen::FixedVoltageNode) {
            return RawElement{ rule.kind,
                *node_a, UINT32_MAX,
                read_param_or(dev, rule.param_a, rule.param_a_default),
                0.0f, element_idx++, dev.name, dev.classname };
        }

        auto node_b = resolve_port_optional(dev.name, dev.classname, rule.port_b, port_to_signal, options);
        if (!node_b.has_value()) return std::nullopt;

        float value_a = read_param_or(dev, rule.param_a, rule.param_a_default);
        float value_b = (rule.param_b != nullptr)
            ? read_param_or(dev, rule.param_b, rule.param_b_default) : 0.0f;

        return RawElement{ rule.kind,
            *node_a, *node_b, value_a, value_b, element_idx++, dev.name, dev.classname };
    }

    return std::nullopt;
}

// ===== Section 4: Disjoint Set Union for Island Building =====
// Simple union-find data structure with path compression and union by rank.
struct DisjointSet {
    std::unordered_map<uint32_t, uint32_t> parent;
    std::unordered_map<uint32_t, uint32_t> rank;

    void init(const std::unordered_set<uint32_t>& nodes) {
        for (uint32_t n : nodes) {
            parent[n] = n;
            rank[n] = 0;
        }
    }

    uint32_t find(uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);
        if (ra == rb) {
            return;
        }
        if (rank[ra] < rank[rb]) {
            std::swap(ra, rb);
        }
        parent[rb] = ra;
        if (rank[ra] == rank[rb]) {
            rank[ra]++;
        }
    }
};

// ===== Section 5: Island Construction =====
// Build electrical islands from raw elements using union-find.
// LOC: ~50 (tight coupling to island grouping and node collection)
void build_electrical_islands(
    const std::vector<RawElement>& raw_elements,
    ElectricalPlanCodegen& plan
) {
    if (raw_elements.empty()) {
        return;
    }

    // Collect all nodes and initialize union-find
    std::unordered_set<uint32_t> all_nodes;
    for (const auto& elem : raw_elements) {
        all_nodes.insert(elem.node_a);
        if (elem.node_b != UINT32_MAX) {
            all_nodes.insert(elem.node_b);
        }
    }

    DisjointSet uf;
    uf.init(all_nodes);

    // Union nodes for each element
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

    // Build island structures (stable order by root)
    std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
        island_members.begin(), island_members.end());
    std::sort(sorted_islands.begin(), sorted_islands.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [root, elem_indices] : sorted_islands) {
        (void)root;
        ElectricalIslandPlanCodegen island;

        std::set<uint32_t> island_nodes;
        for (size_t idx : elem_indices) {
            island_nodes.insert(raw_elements[idx].node_a);
            if (raw_elements[idx].node_b != UINT32_MAX) {
                island_nodes.insert(raw_elements[idx].node_b);
            }
        }
        island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

        std::vector<size_t> sorted_indices = elem_indices;
        std::sort(sorted_indices.begin(), sorted_indices.end());
        for (size_t idx : sorted_indices) {
            const auto& re = raw_elements[idx];
            island.elements.push_back({
                re.kind,
                re.node_a,
                re.node_b,
                re.value_a,
                re.value_b,
                static_cast<uint32_t>(re.element_id)
            });
        }

        plan.islands.push_back(std::move(island));
    }
}

// ===== Section 6: Device Bindings for Wrapper Components =====
// Build stable symbolic binding list mapping wrapper components to electrical islands.
// For KnobSwitch: produces N indexed bindings (device_0, device_1, ...) since
// one KnobSwitch generates N ConductanceBranch elements (one per throw terminal).
// LOC: ~80
void build_device_bindings(
    const std::vector<RawElement>& raw_elements,
    const ElectricalPlanCodegen& plan,
    std::vector<ElectricalPlanCodegen::DeviceBinding>& bindings
) {
    // Map element_id -> (island_index, element_index)
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> element_to_island_elem;
    for (size_t island_i = 0; island_i < plan.islands.size(); ++island_i) {
        const auto& island = plan.islands[island_i];
        for (size_t elem_i = 0; elem_i < island.elements.size(); ++elem_i) {
            const auto& elem = island.elements[elem_i];
            element_to_island_elem[elem.element_id] = {
                static_cast<uint32_t>(island_i),
                static_cast<uint32_t>(elem_i)
            };
        }
    }

    // Wrapper components that need electrical handles (non-KnobSwitch: single binding)
    static const std::set<std::string> wrapper_classnames{
        "Generator", "IndicatorLight", "CurrentSense",
        "ControlledVoltageSource", "VariableConductance",
        "AZS", "HoldButton", "Relay"
    };

    // Group raw elements by sanitized device name for KnobSwitch multi-handle support
    std::unordered_map<std::string, std::vector<size_t>> device_elements;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        const auto& re = raw_elements[i];
        device_elements[codegen_detail::sanitize_name(re.device_name)].push_back(i);
    }

    std::vector<ElectricalPlanCodegen::DeviceBinding> tmp_bindings;
    tmp_bindings.reserve(element_to_island_elem.size());

    for (const auto& [element_id, pos] : element_to_island_elem) {
        auto raw_it = std::find_if(raw_elements.begin(), raw_elements.end(),
            [element_id](const RawElement& re) { return re.element_id == element_id; });
        if (raw_it == raw_elements.end()) continue;
        const auto& re = *raw_it;

        const std::string base_name = codegen_detail::sanitize_name(re.device_name);
        const auto kind_opt = parse_component_kind(re.device_classname);
        if (!kind_opt.has_value()) continue;  // unknown classname — skip binding
        const bool is_knob_switch = is_knob_switch_kind(*kind_opt);

        if (is_knob_switch) {
            // Count how many elements this device has produced (for 0-based indexing)
            const auto& indices = device_elements.at(base_name);
            size_t elem_position = 0;
            for (size_t idx : indices) {
                if (raw_elements[idx].element_id == element_id) break;
                ++elem_position;
            }
            std::string indexed_name = base_name + "_" + std::to_string(elem_position);
            tmp_bindings.push_back({
                indexed_name,
                pos.first,
                pos.second,
                element_id
            });
        } else if (wrapper_classnames.count(re.device_classname) > 0) {
            tmp_bindings.push_back({
                base_name,
                pos.first,
                pos.second,
                element_id
            });
        }
    }

    // Sort and deduplicate
    std::sort(tmp_bindings.begin(), tmp_bindings.end(), [](const auto& a, const auto& b) {
        if (a.element_id != b.element_id) return a.element_id < b.element_id;
        if (a.island_index != b.island_index) return a.island_index < b.island_index;
        if (a.element_index != b.element_index) return a.element_index < b.element_index;
        return a.device_field_name < b.device_field_name;
    });
    tmp_bindings.erase(std::unique(tmp_bindings.begin(), tmp_bindings.end(), [](const auto& a, const auto& b) {
        return a.device_field_name == b.device_field_name &&
               a.island_index == b.island_index &&
               a.element_index == b.element_index &&
               a.element_id == b.element_id;
    }), tmp_bindings.end());

    bindings = std::move(tmp_bindings);
}

// ===== Section 7: Debug Metadata =====
// Build component debug tables for introspection and troubleshooting.
// LOC: ~50 (metadata collection + sorting)
void build_component_debug(
    const std::vector<RawElement>& raw_elements,
    const ElectricalPlanCodegen& plan,
    std::vector<ElectricalPlanCodegen::ComponentDebug>& debug
) {
    std::unordered_map<uint32_t, size_t> element_to_raw_idx;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        element_to_raw_idx[static_cast<uint32_t>(raw_elements[i].element_id)] = i;
    }

    std::vector<ElectricalPlanCodegen::ComponentDebug> tmp_debug;
    tmp_debug.reserve(raw_elements.size());
    for (size_t island_i = 0; island_i < plan.islands.size(); ++island_i) {
        const auto& island = plan.islands[island_i];
        for (size_t elem_i = 0; elem_i < island.elements.size(); ++elem_i) {
            const auto& elem = island.elements[elem_i];
            auto raw_it = element_to_raw_idx.find(elem.element_id);
            if (raw_it == element_to_raw_idx.end()) {
                continue;
            }
            const auto& re = raw_elements[raw_it->second];
            tmp_debug.push_back({
                static_cast<uint32_t>(re.element_id),
                static_cast<uint32_t>(island_i),
                static_cast<uint32_t>(elem_i),
                re.device_name,
                re.device_classname,
                kind_to_role(re.kind),
                re.node_a,
                re.node_b
            });
        }
    }

    std::sort(tmp_debug.begin(), tmp_debug.end(), [](const auto& a, const auto& b) {
        if (a.element_id != b.element_id) return a.element_id < b.element_id;
        if (a.island_index != b.island_index) return a.island_index < b.island_index;
        if (a.element_index != b.element_index) return a.element_index < b.element_index;
        return a.device_name < b.device_name;
    });

    debug = std::move(tmp_debug);
}

// ===== Section 8: Patch Op Generation =====
// Build NodalPatchOp arrays from device data for AOT codegen.
// Mirrors the JIT build_electrical_patch_ops() logic but uses ResolvedDevice
// data instead of live component variants.

/// Look up a signal index by device name and port name. Returns UINT32_MAX if not found.
static uint32_t lookup_signal(
    const std::string& device_name,
    const std::string& port_name,
    const std::unordered_map<std::string, uint32_t>& port_to_signal)
{
    std::string key = signal_key::make_node_port_key(device_name, port_name);
    auto it = port_to_signal.find(key);
    return (it != port_to_signal.end()) ? it->second : UINT32_MAX;
}

/// Look up a float parameter from a ResolvedDevice. Returns default_val if not found.
static float lookup_param(
    const ResolvedDevice& dev,
    const std::string& param_name,
    float default_val)
{
    auto it = dev.params.find(param_name);
    if (it == dev.params.end()) return default_val;
    return locale_safe::parse_float_or(it->second, default_val);
}

/// Look up element_id for a device from the DeviceBinding list. Returns UINT32_MAX if not found.
static uint32_t lookup_element_id(
    const std::string& device_name,
    const std::unordered_map<std::string, uint32_t>& binding_map)
{
    auto it = binding_map.find(device_name);
    return (it != binding_map.end()) ? it->second : UINT32_MAX;
}

void build_patch_ops(
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const std::vector<ElectricalPlanCodegen::DeviceBinding>& bindings,
    std::vector<NodalPatchOp>& patch_ops)
{
    patch_ops.clear();

    // Build O(1) lookup map from device field name to element_id.
    std::unordered_map<std::string, uint32_t> binding_map;
    binding_map.reserve(bindings.size());
    for (const auto& b : bindings) {
        binding_map[b.device_field_name] = b.element_id;
    }

    for (const auto& dev : devices) {
        const std::string field = codegen_detail::sanitize_name(dev.name);

        if (dev.kind == ComponentKind::ControlledVoltageSource) {
            uint32_t eid = lookup_element_id(field, binding_map);
            if (eid == UINT32_MAX) continue;
            NodalPatchOp op;
            op.kind = NodalPatchKind::AffineClamp;
            op.element_id = eid;
            op.s0 = lookup_signal(dev.name, "cmd", port_to_signal);
            op.s1 = lookup_signal(dev.name, "gain", port_to_signal);
            op.s2 = lookup_signal(dev.name, "offset", port_to_signal);
            op.s3 = lookup_signal(dev.name, "min_v", port_to_signal);
            op.s4 = lookup_signal(dev.name, "max_v", port_to_signal);
            if (op.s0 == UINT32_MAX) continue;
            patch_ops.push_back(op);
        }
        else if (dev.kind == ComponentKind::VariableConductance) {
            uint32_t eid = lookup_element_id(field, binding_map);
            if (eid == UINT32_MAX) continue;
            NodalPatchOp op;
            op.kind = NodalPatchKind::LerpClamped01;
            op.element_id = eid;
            op.s0 = lookup_signal(dev.name, "cmd", port_to_signal);
            op.s1 = lookup_signal(dev.name, "g_min", port_to_signal);
            op.s2 = lookup_signal(dev.name, "g_max", port_to_signal);
            if (op.s0 == UINT32_MAX) continue;
            patch_ops.push_back(op);
        }
        else if (dev.kind == ComponentKind::AZS ||
                 dev.kind == ComponentKind::HoldButton ||
                 dev.kind == ComponentKind::Relay) {
            uint32_t eid = lookup_element_id(field, binding_map);
            if (eid == UINT32_MAX) continue;
            NodalPatchOp op;
            op.kind = NodalPatchKind::BoolSwitch;
            op.element_id = eid;
            op.s0 = lookup_signal(dev.name, "state", port_to_signal);
            op.state_true_value = lookup_param(dev, "g_closed", 1000.0f);
            op.state_false_value = lookup_param(dev, "g_open", 1e-6f);
            if (op.s0 == UINT32_MAX) continue;
            patch_ops.push_back(op);
        }
        else if (is_knob_switch_kind(dev.kind)) {
            // IndexSwitch: one patch op per handle (throw position).
            // KnobSwitch generates multiple ConductanceBranch elements, each with
            // its own binding named "device_0", "device_1", etc.
            int num_positions = static_cast<int>(lookup_param(dev, "positions", 2.0f));
            float g_closed = lookup_param(dev, "g_closed", 1000.0f);
            float g_open = lookup_param(dev, "g_open", 1e-6f);

            for (int i = 0; i < num_positions; ++i) {
                std::string indexed_name = field + "_" + std::to_string(i);
                uint32_t eid = lookup_element_id(indexed_name, binding_map);
                if (eid == UINT32_MAX) continue;
                NodalPatchOp op;
                op.kind = NodalPatchKind::IndexSwitch;
                op.element_id = eid;
                op.s0 = lookup_signal(dev.name, "position", port_to_signal);
                op.index_value = i;
                op.state_true_value = g_closed;
                op.state_false_value = g_open;
                if (op.s0 == UINT32_MAX) continue;
                patch_ops.push_back(op);
            }
        }
    }
}

// ===== Main Entry Point: extract_electrical_plan() =====
// Orchestrate extraction of electrical elements, island building, and binding generation.
// LOC: ~80 (clean delegation to helpers, low complexity)
ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options
) {
    ElectricalPlanCodegen plan;

    // Collect electrical elements from devices (phase 1: extraction)
    std::vector<RawElement> raw_elements;
    size_t element_idx = 0;

    auto device_has_any_ports = [&](const ResolvedDevice& dev) -> bool {
        for (const auto& [port_name, _port] : dev.ports) {
            std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
            if (port_to_signal.find(full_port) != port_to_signal.end()) {
                return true;
            }
        }
        return false;
    };

    for (const auto& dev : devices) {
        if (!device_has_any_ports(dev)) {
            continue;
        }

        if (dev.solver_role.has_value()) {
            const auto& role = *dev.solver_role;
            // Skip solver_roles for non-electrical domains (e.g., Hydraulic).
            // These are handled by their respective domain extractors.
            if (role.domain != Domain::Electrical) {
                continue;
            }
            auto elems = extract_solver_role_element(dev, port_to_signal, options, element_idx);
            for (auto& elem : elems) {
                raw_elements.push_back(std::move(elem));
            }
            continue;
        }

        // Fall back to classname-based rules
        auto elem_opt = extract_classname_rule_element(dev, port_to_signal, options, element_idx);
        if (elem_opt.has_value()) {
            raw_elements.push_back(std::move(*elem_opt));
        }
    }

    if (raw_elements.empty()) {
        return plan;
    }

    // Build islands from raw elements (phase 2: island construction)
    build_electrical_islands(raw_elements, plan);

    // Build device bindings and debug metadata (phase 3: bindings & debug)
    build_device_bindings(raw_elements, plan, plan.device_bindings);
    build_component_debug(raw_elements, plan, plan.component_debug);

    // Build patch ops for dynamic source patching (phase 4: patch ops)
    build_patch_ops(devices, port_to_signal, plan.device_bindings, plan.patch_ops);

    return plan;
}
