#include "codegen.h"
#include "codegen_internal.h"
#include "core/solvers/common/build_algorithms.h"
#include "core/solvers/common/signal_key.h"
#include "core/solvers/common/nodal_patch_convert.h"
#include "parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <optional>
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

} // anonymous namespace

// ===== Section 1: Helper Types & Functions for Element Extraction =====
// AOT-specific raw element — standalone struct (not inheriting from GenericRawElement)
// for clean aggregate initialization. Compatible with build_algo templates via
// duck-typed field access (kind, node_a, node_b, value_a, value_b, element_id, device_name).
struct RawElement {
    NodalElementKind kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    size_t element_id;
    std::string device_name;
    std::string device_classname;  // AOT-only: needed for debug metadata
};

/// Convert NodalElementKind to legacy debug role string.
/// These names are preserved for backward compatibility in generated debug tables.
std::string kind_to_role(NodalElementKind k) {
    switch (k) {
        case NodalElementKind::FixedNode: return "FixedVoltageNode";
        case NodalElementKind::Source:    return "TheveninSource";
        case NodalElementKind::Branch:    return "ConductanceBranch";
    }
    return "Unknown";
}

// ===== Section 2: Raw Element Collection (solver_role path) =====
// Extract electrical elements from device with explicit solver_role.
// Handles four element kinds: FixedNode, Source, Branch, KnobSwitchBranches.
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
        result.push_back({ NodalElementKind::FixedNode,
            *node, UINT32_MAX, value, 0.0f, element_idx++, dev.name, dev.classname });
        return result;
    }

    if (role.kind == SolverRoleKind::TheveninSource) {
        float voltage = read_role_param(role, dev, "voltage", 28.0f);
        float resistance = read_role_param(role, dev, "resistance", 0.01f);
        auto pos = resolve_port_optional(dev.name, dev.classname, role.port_map.at("pos"), port_to_signal, options);
        auto neg = resolve_port_optional(dev.name, dev.classname, role.port_map.at("neg"), port_to_signal, options);
        if (!pos.has_value() || !neg.has_value()) return result;
        result.push_back({ NodalElementKind::Source,
            *pos, *neg, voltage, resistance, element_idx++, dev.name, dev.classname });
        return result;
    }

    if (role.kind == SolverRoleKind::ConductanceBranch) {
        float g = read_role_param(role, dev, "g", 0.1f);
        auto a = resolve_port_optional(dev.name, dev.classname, role.port_map.at("a"), port_to_signal, options);
        auto b = resolve_port_optional(dev.name, dev.classname, role.port_map.at("b"), port_to_signal, options);
        if (!a.has_value() || !b.has_value()) return result;
        result.push_back({ NodalElementKind::Branch,
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
            result.push_back({ NodalElementKind::Branch,
                *node_wiper, *node_t, g, 0.0f, element_idx++, dev.name, dev.classname });
        }
        return result;
    }

    throw std::runtime_error(
        "[codegen] unsupported solver_role kind '" + std::string(solver_role_kind_name(role.kind)) +
        "' for device '" + dev.name + "' (classname: " + dev.classname + ")");
}

// ===== Section 3: Device Bindings for Wrapper Components =====
// Build stable symbolic binding list mapping wrapper components to electrical islands.
// For KnobSwitch: produces N indexed bindings (device_0, device_1, ...) since
// one KnobSwitch generates N Branch elements (one per throw terminal).
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

    // Data-driven: any raw element with a non-empty device_name gets a binding.
    // The device_name is only set when bind_handle is true in the solver_role.

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
        } else if (!re.device_name.empty()) {
            // Single-handle binding: any element with bind_handle=true gets a binding.
            // The device_name is set by the extractor only when bind_handle is true.
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

// ===== Section 4: Debug Metadata =====
// Build component debug tables for introspection and troubleshooting.
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

// ===== Section 5: AOT Patch Op Context =====
// Adapts AOT's string-keyed signal resolution for the generic patch op builder.

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

/// AOT-specific context adapter for the generic patch op builder.
struct AotPatchOpContext {
    const std::vector<ResolvedDevice>& devices;
    const std::unordered_map<std::string, uint32_t>& port_to_signal;
    const std::unordered_map<std::string, uint32_t>& binding_map;

    size_t device_count() const { return devices.size(); }

    bool has_patch_op(size_t i) const {
        return devices[i].solver_role.has_value() &&
               devices[i].solver_role->patch_op.has_value();
    }

    const PatchOpDecl& patch_op_decl(size_t i) const {
        return *devices[i].solver_role->patch_op;
    }

    /// AOT uses sanitized device names as element keys (matching DeviceBinding).
    std::string device_element_key(size_t i, int handle_index = -1) const {
        std::string base = codegen_detail::sanitize_name(devices[i].name);
        return handle_index >= 0
            ? base + "_" + std::to_string(handle_index)
            : base;
    }

    uint32_t lookup_element_id(const std::string& key) const {
        auto it = binding_map.find(key);
        return (it != binding_map.end()) ? it->second : UINT32_MAX;
    }

    void fill_signal_ports(NodalPatchOp& op, const PatchOpDecl& decl, size_t i) const {
        const size_t n_signals = std::min(decl.signal_ports.size(), size_t(5));
        uint32_t* targets[] = { &op.s0, &op.s1, &op.s2, &op.s3, &op.s4 };
        for (size_t j = 0; j < n_signals; ++j) {
            *targets[j] = lookup_signal(devices[i].name, decl.signal_ports[j], port_to_signal);
        }
        if (!decl.true_value_param.empty()) {
            op.state_true_value = lookup_param(devices[i], decl.true_value_param, 0.0f);
        }
        if (!decl.false_value_param.empty()) {
            op.state_false_value = lookup_param(devices[i], decl.false_value_param, 0.0f);
        }
    }
};

// ===== Main Entry Point: extract_electrical_plan() =====
// Orchestrate extraction of electrical elements, island building, and binding generation.
ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options
) {
    ElectricalPlanCodegen plan;

    // Phase 1: Extract electrical elements from devices
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

        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;

        // Skip solver_roles for non-electrical domains.
        if (role.domain != Domain::Electrical) {
            continue;
        }

        auto elems = extract_solver_role_element(dev, port_to_signal, options, element_idx);
        for (auto& elem : elems) {
            raw_elements.push_back(std::move(elem));
        }
    }

    if (raw_elements.empty()) {
        return plan;
    }

    // Phase 2: Build islands using shared algorithm (replaces DisjointSet + build_electrical_islands)
    build_algo::group_into_islands<RawElement, NodalIslandPlan, ElectricalPlanCodegen>(
        raw_elements, plan);

    // Phase 3: Build device bindings and debug metadata
    build_device_bindings(raw_elements, plan, plan.device_bindings);
    build_component_debug(raw_elements, plan, plan.component_debug);

    // Phase 4: Build patch ops using shared generic builder with AOT context adapter
    std::unordered_map<std::string, uint32_t> binding_map;
    binding_map.reserve(plan.device_bindings.size());
    for (const auto& b : plan.device_bindings) {
        binding_map[b.device_field_name] = b.element_id;
    }
    AotPatchOpContext ctx{devices, port_to_signal, binding_map};
    build_algo::build_patch_ops_generic(plan.patch_ops, ctx);

    return plan;
}
