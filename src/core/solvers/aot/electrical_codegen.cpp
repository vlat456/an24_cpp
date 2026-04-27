#include "codegen.h"
#include "codegen_internal.h"
#include "core/solvers/common/build_algorithms.h"
#include "core/solvers/common/element_extraction.h"
#include "core/solvers/common/signal_key.h"
#include "core/solvers/common/nodal_patch_convert.h"
#include "parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace {

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

// ===== Section 1: Helper Types =====

// AOT-specific raw element — standalone struct for clean aggregate initialization.
// Compatible with build_algo templates via duck-typed field access.
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

/// Convert NodalElementKind to legacy debug role string for generated debug tables.
std::string kind_to_role(NodalElementKind k) {
    switch (k) {
        case NodalElementKind::FixedNode: return "FixedVoltageNode";
        case NodalElementKind::Source:    return "TheveninSource";
        case NodalElementKind::Branch:    return "ConductanceBranch";
    }
    return "Unknown";
}

// ===== Section 2: AOT Extraction Adapter =====
// Implements the ExtractionAdapter concept from element_extraction.h.
// Lenient: returns defaults for missing params, skips elements with missing ports.

struct AotExtractionAdapter {
    const ResolvedDevice& dev;
    const std::unordered_map<std::string, uint32_t>& port_to_signal;
    const ElectricalExtractOptions& options;
    std::vector<RawElement>& out;
    size_t& element_idx;

    float read_param(const SolverRole& role, const char* key, float default_val) {
        // Check value_map first (inline constant values)
        auto it_val = role.value_map.find(key);
        if (it_val != role.value_map.end()) return it_val->second;
        // Then param_map → dev.params
        auto it = role.param_map.find(key);
        if (it == role.param_map.end()) return default_val;
        auto it_param = dev.params.find(it->second);
        if (it_param == dev.params.end()) return default_val;
        return locale_safe::parse_float_or(it_param->second, default_val);
    }

    std::optional<uint32_t> resolve_port(const SolverRole& role, const char* key) {
        auto it = role.port_map.find(key);
        if (it == role.port_map.end()) {
            throw std::runtime_error(
                "[codegen] solver_role missing required port key '" +
                std::string(key) + "' for device '" + dev.name +
                "' (classname: " + dev.classname + ")");
        }
        return resolve_port_optional(
            dev.name, dev.classname, it->second, port_to_signal, options);
    }

    void emit(NodalElementKind kind, uint32_t node_a, uint32_t node_b,
              float value_a, float value_b) {
        out.push_back({kind, node_a, node_b, value_a, value_b,
            element_idx++, dev.name, dev.classname});
    }
};

static_assert(build_algo::ExtractionAdapter<AotExtractionAdapter>,
    "AotExtractionAdapter must satisfy ExtractionAdapter concept");

// ===== Section 3: Device Bindings =====
// Build stable symbolic binding list mapping wrapper components to electrical islands.
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

    // Build O(1) lookup: element_id → raw element index
    std::unordered_map<uint32_t, size_t> element_id_to_raw_idx;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        element_id_to_raw_idx[static_cast<uint32_t>(raw_elements[i].element_id)] = i;
    }

    // Group raw elements by sanitized device name for multi-handle support
    std::unordered_map<std::string, std::vector<size_t>> device_elements;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        const auto& re = raw_elements[i];
        device_elements[codegen_detail::sanitize_name(re.device_name)].push_back(i);
    }

    std::vector<ElectricalPlanCodegen::DeviceBinding> tmp_bindings;
    tmp_bindings.reserve(element_to_island_elem.size());

    for (const auto& [element_id, pos] : element_to_island_elem) {
        auto raw_it = element_id_to_raw_idx.find(element_id);
        if (raw_it == element_id_to_raw_idx.end()) continue;
        const auto& re = raw_elements[raw_it->second];

        const std::string base_name = codegen_detail::sanitize_name(re.device_name);
        const auto kind_opt = parse_component_kind(re.device_classname);
        if (!kind_opt.has_value()) continue;
        const bool is_knob_switch = is_knob_switch_kind(*kind_opt);

        if (is_knob_switch) {
            const auto& indices = device_elements.at(base_name);
            size_t elem_position = 0;
            for (size_t idx : indices) {
                if (raw_elements[idx].element_id == element_id) break;
                ++elem_position;
            }
            std::string indexed_name = base_name + "_" + std::to_string(elem_position);
            tmp_bindings.push_back({indexed_name, pos.first, pos.second, element_id});
        } else if (!re.device_name.empty()) {
            tmp_bindings.push_back({base_name, pos.first, pos.second, element_id});
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
            if (raw_it == element_to_raw_idx.end()) continue;
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

static uint32_t lookup_signal(
    const std::string& device_name,
    const std::string& port_name,
    const std::unordered_map<std::string, uint32_t>& port_to_signal)
{
    std::string key = signal_key::make_node_port_key(device_name, port_name);
    auto it = port_to_signal.find(key);
    return (it != port_to_signal.end()) ? it->second : UINT32_MAX;
}

static float lookup_param(
    const ResolvedDevice& dev,
    const std::string& param_name,
    float default_val)
{
    auto it = dev.params.find(param_name);
    if (it == dev.params.end()) return default_val;
    return locale_safe::parse_float_or(it->second, default_val);
}

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

// ===== Main Entry Point =====
ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options
) {
    ElectricalPlanCodegen plan;

    // Phase 1: Extract electrical elements using shared extraction + AOT adapter
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

    using ExtractorEntry = build_algo::ExtractorEntry<AotExtractionAdapter>;
    constexpr size_t k_extractor_count = std::size(build_algo::k_electrical_extractors<AotExtractionAdapter>);

    for (const auto& dev : devices) {
        if (!device_has_any_ports(dev)) continue;
        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;
        if (role.domain != Domain::Electrical) continue;

        AotExtractionAdapter adapter{dev, port_to_signal, options, raw_elements, element_idx};
        if (!build_algo::extract_with_table(
                adapter,
                build_algo::k_electrical_extractors<AotExtractionAdapter>,
                k_extractor_count,
                role)) {
            throw std::runtime_error(
                "[codegen] unsupported solver_role kind '" + std::string(solver_role_kind_name(role.kind)) +
                "' for device '" + dev.name + "' (classname: " + dev.classname + ")");
        }
    }

    if (raw_elements.empty()) return plan;

    // Phase 2: Build islands using shared algorithm
    build_algo::group_into_islands<RawElement, NodalIslandPlan, ElectricalPlanCodegen>(
        raw_elements, plan);

    // Phase 3: Build device bindings and debug metadata
    build_device_bindings(raw_elements, plan, plan.device_bindings);
    build_component_debug(raw_elements, plan, plan.component_debug);

    // Phase 4: Build patch ops using shared generic builder
    std::unordered_map<std::string, uint32_t> binding_map;
    binding_map.reserve(plan.device_bindings.size());
    for (const auto& b : plan.device_bindings) {
        binding_map[b.device_field_name] = b.element_id;
    }
    AotPatchOpContext ctx{devices, port_to_signal, binding_map};
    build_algo::build_patch_ops_generic(plan.patch_ops, ctx);

    return plan;
}
