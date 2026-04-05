#include "codegen.h"
#include "codegen_utils.h"
#include "../parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {

float parse_float_codegen(const std::string& value, float default_val) {
    if (value.empty()) {
        return default_val;
    }
    return locale_safe::parse_float_or(value, default_val);
}

float read_param_or(const DeviceInstance& dev, const char* key, float default_val) {
    auto it = dev.params.find(key);
    if (it == dev.params.end()) {
        return default_val;
    }
    return parse_float_codegen(it->second, default_val);
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
// Extract electrical element from device with explicit solver_role.
// Handles three element kinds: FixedVoltageNode, TheveninSource, ConductanceBranch.
std::optional<RawElement> extract_solver_role_element(
    const DeviceInstance& dev,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options,
    size_t& element_idx
) {
    const auto& role = *dev.solver_role;

    auto resolve_port = [&](const std::string& port_name) -> std::optional<uint32_t> {
        const std::string full_port = dev.name + "." + port_name;
        auto it = port_to_signal.find(full_port);
        if (it == port_to_signal.end()) {
            if (options.strict_port_resolution) {
                throw std::runtime_error(
                    "[codegen] electrical port '" + full_port +
                    "' not found in signal map for device '" + dev.name +
                    "' (classname: " + dev.classname + ")"
                );
            }
            if (options.warn_on_missing_ports) {
                spdlog::warn("[codegen] electrical port '{}' not found in signal map for device '{}' (classname: {}). Element skipped from electrical plan.",
                    full_port, dev.name, dev.classname);
            }
            return std::nullopt;
        }
        return it->second;
    };

    if (role.kind == "FixedVoltageNode") {
        float value = 0.0f;
        auto it_val = role.param_map.find("voltage");
        if (it_val != role.param_map.end()) {
            auto it_param = dev.params.find(it_val->second);
            if (it_param != dev.params.end()) {
                value = parse_float_codegen(it_param->second, 0.0f);
            }
        }
        auto node_a_opt = resolve_port(role.port_map.at("node"));
        if (!node_a_opt.has_value()) {
            return std::nullopt;
        }
        return RawElement{
            ElectricalElementKindCodegen::FixedVoltageNode,
            *node_a_opt, UINT32_MAX, value, 0.0f, element_idx++,
            dev.name, dev.classname
        };
    }

    if (role.kind == "TheveninSource") {
        float voltage = 28.0f;
        float resistance = 0.01f;
        auto it_v = role.param_map.find("voltage");
        if (it_v != role.param_map.end()) {
            auto it_param = dev.params.find(it_v->second);
            if (it_param != dev.params.end()) {
                voltage = parse_float_codegen(it_param->second, 28.0f);
            }
        }
        auto it_r = role.param_map.find("resistance");
        if (it_r != role.param_map.end()) {
            auto it_param = dev.params.find(it_r->second);
            if (it_param != dev.params.end()) {
                resistance = parse_float_codegen(it_param->second, 0.01f);
            }
        }
        auto node_pos_opt = resolve_port(role.port_map.at("pos"));
        auto node_neg_opt = resolve_port(role.port_map.at("neg"));
        if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) {
            return std::nullopt;
        }
        return RawElement{
            ElectricalElementKindCodegen::TheveninSource,
            *node_pos_opt, *node_neg_opt, voltage, resistance, element_idx++,
            dev.name, dev.classname
        };
    }

    if (role.kind == "ConductanceBranch") {
        float conductance = 0.1f;
        auto it_g = role.param_map.find("g");
        if (it_g != role.param_map.end()) {
            auto it_param = dev.params.find(it_g->second);
            if (it_param != dev.params.end()) {
                conductance = parse_float_codegen(it_param->second, 0.1f);
            }
        }
        auto node_a_opt = resolve_port(role.port_map.at("a"));
        auto node_b_opt = resolve_port(role.port_map.at("b"));
        if (!node_a_opt.has_value() || !node_b_opt.has_value()) {
            return std::nullopt;
        }
        return RawElement{
            ElectricalElementKindCodegen::ConductanceBranch,
            *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++,
            dev.name, dev.classname
        };
    }

    return std::nullopt;
}

// ===== Section 3: Raw Element Collection (classname rule path) =====
// Extract electrical element from device using classname-based rules.
std::optional<RawElement> extract_classname_rule_element(
    const DeviceInstance& dev,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options,
    size_t& element_idx
) {
    auto resolve_port = [&](const std::string& port_name) -> std::optional<uint32_t> {
        const std::string full_port = dev.name + "." + port_name;
        auto it = port_to_signal.find(full_port);
        if (it == port_to_signal.end()) {
            if (options.strict_port_resolution) {
                throw std::runtime_error(
                    "[codegen] electrical port '" + full_port +
                    "' not found in signal map for device '" + dev.name +
                    "' (classname: " + dev.classname + ")"
                );
            }
            if (options.warn_on_missing_ports) {
                spdlog::warn("[codegen] electrical port '{}' not found in signal map for device '{}' (classname: {}). Element skipped from electrical plan.",
                    full_port, dev.name, dev.classname);
            }
            return std::nullopt;
        }
        return it->second;
    };

    for (const auto& rule : k_classname_rules) {
        if (dev.classname != rule.classname) {
            continue;
        }

        auto node_a_opt = resolve_port(rule.port_a);
        if (!node_a_opt.has_value()) {
            return std::nullopt;
        }

        if (rule.kind == ElectricalElementKindCodegen::FixedVoltageNode) {
            return RawElement{
                rule.kind,
                *node_a_opt,
                UINT32_MAX,
                read_param_or(dev, rule.param_a, rule.param_a_default),
                0.0f,
                element_idx++,
                dev.name, dev.classname
            };
        }

        auto node_b_opt = resolve_port(rule.port_b);
        if (!node_b_opt.has_value()) {
            return std::nullopt;
        }

        float value_a = read_param_or(dev, rule.param_a, rule.param_a_default);
        float value_b = 0.0f;
        if (rule.param_b != nullptr) {
            value_b = read_param_or(dev, rule.param_b, rule.param_b_default);
        }

        return RawElement{
            rule.kind,
            *node_a_opt,
            *node_b_opt,
            value_a,
            value_b,
            element_idx++,
            dev.name, dev.classname
        };
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
// LOC: ~60 (filtering + sorting + dedup)
void build_device_bindings(
    const std::vector<RawElement>& raw_elements,
    const ElectricalPlanCodegen& plan,
    std::vector<ElectricalPlanCodegen::DeviceBinding>& bindings
) {
    // Map wrapper components to electrical island positions
    std::unordered_map<uint32_t, size_t> element_to_raw_idx;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        element_to_raw_idx[static_cast<uint32_t>(raw_elements[i].element_id)] = i;
    }

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

    // Wrapper components that need electrical handles
    static const std::set<std::string> wrapper_classnames{
        "Generator", "IndicatorLight", "CurrentSense",
        "ControlledVoltageSource", "VariableConductance",
        "AZS", "HoldButton"
    };

    std::vector<ElectricalPlanCodegen::DeviceBinding> tmp_bindings;
    tmp_bindings.reserve(element_to_island_elem.size());
    for (const auto& [element_id, pos] : element_to_island_elem) {
        auto raw_it = element_to_raw_idx.find(element_id);
        if (raw_it == element_to_raw_idx.end()) {
            continue;
        }
        const auto& re = raw_elements[raw_it->second];
        if (wrapper_classnames.count(re.device_classname) > 0) {
            tmp_bindings.push_back({
                sanitize_codegen_name(re.device_name),
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

// ===== Main Entry Point: extract_electrical_plan() =====
// Orchestrate extraction of electrical elements, island building, and binding generation.
// LOC: ~80 (clean delegation to helpers, low complexity)
ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<DeviceInstance>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options
) {
    ElectricalPlanCodegen plan;

    // Collect electrical elements from devices (phase 1: extraction)
    std::vector<RawElement> raw_elements;
    size_t element_idx = 0;

    auto device_has_any_ports = [&](const DeviceInstance& dev) -> bool {
        for (const auto& [port_name, _port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
            if (port_to_signal.find(full_port) != port_to_signal.end()) {
                return true;
            }
        }
        return false;
    };

    for (const auto& dev : devices) {
        if (dev.visual_only || !device_has_any_ports(dev)) {
            continue;
        }

        // Extract from solver_role first (explicit specification)
        if (dev.solver_role.has_value()) {
            auto elem_opt = extract_solver_role_element(dev, port_to_signal, options, element_idx);
            if (elem_opt.has_value()) {
                raw_elements.push_back(std::move(*elem_opt));
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

    return plan;
}
