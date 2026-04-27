#pragma once

/// Shared build algorithms for nodal domain pipelines.
///
/// Pure algorithmic building blocks that require zero solver/device/variant
/// awareness. Both JIT (build_common.h) and AOT (electrical_codegen.cpp)
/// include this header. The JIT layer adds port/param resolution on top;
/// the AOT layer adds codegen-specific metadata on top.
///
/// Dependencies: nodal_types.h, union_find.h, nodal_patch_types.h,
///   nodal_patch_convert.h (→ component_types.h), standard headers only.
///   Zero solver/device/variant awareness.

#include "core/solvers/common/nodal_types.h"
#include "core/utils/union_find.h"
#include "core/solvers/common/nodal_patch_types.h"
#include "core/solvers/common/nodal_patch_convert.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace build_algo {

// =====================================================================
// Generic raw element — parameterized by element kind enum.
// =====================================================================

/// Parameterized raw element used during build extraction.
/// Both JIT and AOT define their own concrete structs (JIT uses this
/// template directly; AOT adds extra fields like device_classname).
/// All template algorithms access fields by name (duck typing).
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

// =====================================================================
// Island grouping via union-find.
// =====================================================================

/// Group raw elements into connected islands using union-find.
///
/// RawElem must have: kind, node_a, node_b, value_a, value_b, element_id.
/// IslandPlan must have: signal_indices (vector<uint32_t>), elements (vector<E>).
/// BuildPlan must have: islands (vector<IslandPlan>).
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
// Element ID map — device_name → element_id mapping.
// =====================================================================

/// Build device_name → element_id map from raw extraction results.
/// For multi-handle devices (same device_name appearing N times), elements
/// are indexed as "device_0", "device_1", etc. to match PatchOpDecl's
/// multi_handle convention.
///
/// RawElem must have: device_name (string), element_id (size_t/uint32_t).
template<typename RawElem>
std::unordered_map<std::string, uint32_t> build_element_id_map(
    const std::vector<RawElem>& raw_elements)
{
    // Group by device_name to detect multi-handle
    std::unordered_map<std::string, std::vector<uint32_t>> device_elements;
    for (const auto& elem : raw_elements) {
        if (!elem.device_name.empty()) {
            device_elements[elem.device_name].push_back(
                static_cast<uint32_t>(elem.element_id));
        }
    }

    std::unordered_map<std::string, uint32_t> result;
    result.reserve(raw_elements.size());
    for (auto& [name, ids] : device_elements) {
        if (ids.size() == 1) {
            result[name] = ids[0];
        } else {
            for (size_t i = 0; i < ids.size(); ++i) {
                result[name + "_" + std::to_string(i)] = ids[i];
            }
        }
    }
    return result;
}

// =====================================================================
// Element value initialization.
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
// Generic patch op builder — works for both JIT and AOT.
//
// Uses a context object (duck-typed) that provides device iteration,
// patch op metadata, element ID lookup, and signal/param resolution.
// This avoids coupling to SolverDevice (JIT) or ResolvedDevice (AOT).
// =====================================================================

/// Build patch ops from a context object.
///
/// Ctx must provide:
///   size_t device_count() const
///   bool has_patch_op(size_t i) const
///   const PatchOpDecl& patch_op_decl(size_t i) const
///   std::string device_element_key(size_t i, int handle_index = -1) const
///   uint32_t lookup_element_id(const std::string& key) const
///   void fill_signal_ports(NodalPatchOp& op, const PatchOpDecl& decl, size_t i) const
template<typename Ctx>
void build_patch_ops_generic(
    std::vector<NodalPatchOp>& patch_ops,
    const Ctx& ctx)
{
    patch_ops.clear();

    for (size_t i = 0; i < ctx.device_count(); ++i) {
        if (!ctx.has_patch_op(i)) continue;

        const auto& decl = ctx.patch_op_decl(i);
        if (decl.kind == PatchOpKind::None) continue;

        if (decl.multi_handle) {
            for (int h = 0; ; ++h) {
                std::string key = ctx.device_element_key(i, h);
                uint32_t eid = ctx.lookup_element_id(key);
                if (eid == UINT32_MAX) break;

                NodalPatchOp op;
                op.kind = to_nodal_patch_kind(decl.kind);
                op.element_id = eid;
                op.index_value = h;
                ctx.fill_signal_ports(op, decl, i);
                if (op.element_id != UINT32_MAX && op.s0 != UINT32_MAX) {
                    patch_ops.push_back(op);
                }
            }
        } else {
            std::string key = ctx.device_element_key(i);
            uint32_t eid = ctx.lookup_element_id(key);
            if (eid == UINT32_MAX) continue;

            NodalPatchOp op;
            op.kind = to_nodal_patch_kind(decl.kind);
            op.element_id = eid;
            ctx.fill_signal_ports(op, decl, i);
            if (op.element_id != UINT32_MAX && op.s0 != UINT32_MAX) {
                patch_ops.push_back(op);
            }
        }
    }
}

} // namespace build_algo
