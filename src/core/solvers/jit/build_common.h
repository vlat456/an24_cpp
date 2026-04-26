#pragma once

/// Shared build helpers used by both electrical and hydraulic build pipelines.
/// Eliminates duplication of resolve_port, union-find, island grouping, etc.

#include "jit_solver.h"
#include "core/solvers/common/signal_key.h"
#include "core/utils/union_find.h"
#include "../../../parse_number.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

namespace jit_solver_impl {
namespace build_common {

// =====================================================================
// Port and param resolution — identical across all nodal domains.
// =====================================================================

inline uint32_t resolve_port(
    const SolverDevice& dev,
    const std::string& port_name,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
    const core::InternedId key = signal_key_interner.lookup(full_port);
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

inline float read_param_float_required(
    const SolverDevice& dev,
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

inline uint32_t resolve_role_port(
    const SolverDevice& dev,
    const SolverRole& role,
    const std::string& role_key,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    auto it = role.port_map.find(role_key);
    if (it == role.port_map.end()) {
        throw std::runtime_error("solver_role missing required port key '" + role_key +
            "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
    }
    return resolve_port(dev, it->second, port_to_signal, signal_key_interner);
}

inline float read_role_param_required(
    const SolverDevice& dev,
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

inline bool should_bind_handle(const SolverRole& role) {
    auto it = role.value_map.find("bind_handle");
    return it != role.value_map.end() && it->second > 0.5f;
}

// =====================================================================
// Generic island grouping — works for any domain's raw elements.
// =====================================================================

/// Generic raw element — parameterized by element kind enum.
template<typename Kind>
struct GenericRawElement {
    Kind kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    size_t element_id;
    std::string device_name;
};

/// Group raw elements into connected islands using union-find.
/// ElementT must have: kind, node_a, node_b, value_a, value_b, element_id.
/// IslandT must have: signal_indices (vector<uint32_t>), elements (vector<Element>).
template<typename RawElem, typename IslandPlan, typename BuildPlan>
void group_into_islands(
    const std::vector<RawElem>& raw_elements,
    BuildPlan& plan)
{
    if (raw_elements.empty()) return;

    std::unordered_set<uint32_t> all_nodes;
    for (const auto& elem : raw_elements) {
        all_nodes.insert(elem.node_a);
        if (elem.node_b != UINT32_MAX) {
            all_nodes.insert(elem.node_b);
        }
    }

    // Size UnionFind to cover all node indices (may be sparse, e.g. {5, 17, 42}).
    // Over-allocating to max_node+1 is negligible for typical signal counts.
    uint32_t max_node = 0;
    for (uint32_t n : all_nodes) {
        max_node = std::max(max_node, n);
    }
    core::utils::UnionFind uf(static_cast<size_t>(max_node) + 1);

    for (const auto& elem : raw_elements) {
        if (elem.node_b != UINT32_MAX) {
            uf.unite(elem.node_a, elem.node_b);
        }
    }

    std::map<uint32_t, std::vector<size_t>> island_members;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        uint32_t root = uf.find(raw_elements[i].node_a);
        island_members[root].push_back(i);
    }

    std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
        island_members.begin(), island_members.end());
    std::sort(sorted_islands.begin(), sorted_islands.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [root, elem_indices] : sorted_islands) {
        (void)root;
        IslandPlan island;

        std::set<uint32_t> island_nodes;
        for (size_t idx : elem_indices) {
            island_nodes.insert(raw_elements[idx].node_a);
            if (raw_elements[idx].node_b != UINT32_MAX) {
                island_nodes.insert(raw_elements[idx].node_b);
            }
        }
        island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

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
// Generic runtime element value initialization.
// =====================================================================

/// Initialize element_value_a from plan defaults. Used by both subsolvers
/// and the simulator's ensure_runtime_element_values.
template<typename BuildPlan, typename RuntimeState>
void init_element_values_from_plan(const BuildPlan& plan, RuntimeState& rt) {
    uint32_t max_element_id = 0;
    bool has_elements = false;
    for (const auto& island : plan.islands) {
        for (const auto& elem : island.elements) {
            has_elements = true;
            max_element_id = std::max(max_element_id, elem.element_id);
        }
    }

    if (!has_elements) {
        rt.element_value_a.clear();
        return;
    }

    const size_t needed = static_cast<size_t>(max_element_id) + 1;
    if (rt.element_value_a.size() < needed) {
        rt.element_value_a.resize(needed, 0.0f);
        for (const auto& island : plan.islands) {
            for (const auto& elem : island.elements) {
                if (elem.element_id < rt.element_value_a.size()) {
                    rt.element_value_a[elem.element_id] = elem.value_a;
                }
            }
        }
    }
}

// =====================================================================
// Generic extractor table — works for any domain's raw elements.
// =====================================================================

/// Extractor function signature: converts a device's solver_role into raw elements.
template<typename RawElem>
using ExtractorFn = void(*)(
    const SolverDevice& dev,
    const SolverRole& role,
    const PortToSignal& port_to_signal,
    const core::StringInterner& interner,
    bool bind_handle,
    std::vector<RawElem>& out,
    size_t& element_idx);

/// One entry in the domain-specific extractor table.
template<typename RawElem>
struct ElementExtractor {
    SolverRoleKind kind;
    ExtractorFn<RawElem> extract;
};

/// Find extractor by SolverRoleKind. Linear scan over small array.
template<typename RawElem>
const ElementExtractor<RawElem>* find_extractor(
    const ElementExtractor<RawElem>* table, size_t count, SolverRoleKind kind)
{
    for (size_t i = 0; i < count; ++i) {
        if (table[i].kind == kind) return &table[i];
    }
    return nullptr;
}

} // namespace build_common
} // namespace jit_solver_impl
