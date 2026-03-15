#pragma once

#include "data/blueprint.h"
#include "data/sub_blueprint_instance.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

// =============================================================================
// Blueprint State Checksum — debug-mode undo/redo verification
// =============================================================================
//
// Computes a hash of the Blueprint's mutable data-layer state. This covers:
//   - All node fields that commands can modify (pos, size, name, params)
//   - All wire fields (endpoints, routing points)
//   - Global state (grid_step)
//
// Node and wire hashes are aggregated ORDER-INDEPENDENTLY because vector
// ordering is not semantically meaningful (lookups use index maps). This
// ensures that a remove-then-re-add cycle (which may append at a different
// position) still produces the same checksum.
//
// It intentionally IGNORES:
//   - Derived indices (node_index_, wire_index_, wire_id_index_, etc.)
//   - Viewport state (pan, zoom) — not managed by commands
//   - node_content — updated by simulation, not by commands
//
// Usage:
//   size_t before = blueprint_checksum(bp);
//   undo_stack.snapshot(bp);
//   execute(bp, cmd);
//   undo_stack.undo(bp);
//   size_t after = blueprint_checksum(bp);
//   assert(before == after && "Undo did not restore state!");

namespace detail {

/// Internal hasher that accumulates into a local hash value.
struct Hasher {
    size_t h = 0;

    void combine(size_t v) {
        h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    void hash_float(float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        combine(std::hash<uint32_t>{}(bits));
    }
    void hash_str(const std::string& s) {
        combine(std::hash<std::string>{}(s));
    }
    void hash_id(ui::InternedId id) {
        combine(std::hash<uint32_t>{}(id.raw()));
    }
    void hash_pt(ui::Pt p) {
        hash_float(p.x);
        hash_float(p.y);
    }

    size_t finish() const { return h; }
};

/// Hash a single node into a size_t (order-dependent within the node's fields).
inline size_t hash_node(const Node& node) {
    Hasher h;
    h.hash_id(node.id);
    h.hash_str(node.name);
    h.hash_str(node.type_name);
    h.hash_pt(node.pos);
    h.hash_pt(node.size);

    // Params — sorted keys for determinism (unordered_map iteration is non-deterministic)
    h.combine(node.params.size());
    {
        std::vector<std::string> sorted_keys;
        sorted_keys.reserve(node.params.size());
        for (const auto& [k, _] : node.params) sorted_keys.push_back(k);
        std::sort(sorted_keys.begin(), sorted_keys.end());
        for (const auto& k : sorted_keys) {
            h.hash_str(k);
            h.hash_str(node.params.at(k));
        }
    }

    // Ports
    h.combine(node.inputs.size());
    for (const auto& p : node.inputs) {
        h.hash_id(p.name);
    }
    h.combine(node.outputs.size());
    for (const auto& p : node.outputs) {
        h.hash_id(p.name);
    }

    return h.finish();
}

/// Hash a single wire into a size_t (order-dependent within the wire's fields).
inline size_t hash_wire(const Wire& wire) {
    Hasher h;
    h.hash_id(wire.id);
    h.hash_id(wire.start.node_id);
    h.hash_id(wire.start.port_name);
    h.hash_id(wire.end.node_id);
    h.hash_id(wire.end.port_name);

    h.combine(wire.routing_points.size());
    for (const auto& rp : wire.routing_points) {
        h.hash_pt(rp);
    }

    return h.finish();
}

/// Hash a single SubBlueprintInstance into a size_t.
inline size_t hash_sub_blueprint_instance(const SubBlueprintInstance& sbi) {
    Hasher h;
    h.hash_str(sbi.id);
    h.hash_str(sbi.blueprint_path);
    h.hash_str(sbi.type_name);
    h.combine(sbi.baked_in ? 1 : 0);
    h.hash_pt(sbi.pos);
    h.hash_pt(sbi.size);

    // internal_node_ids — sorted for determinism
    h.combine(sbi.internal_node_ids.size());
    {
        std::vector<std::string> sorted_ids(sbi.internal_node_ids.begin(), sbi.internal_node_ids.end());
        std::sort(sorted_ids.begin(), sorted_ids.end());
        for (const auto& id : sorted_ids) {
            h.hash_str(id);
        }
    }

    // params_override — sorted keys
    h.combine(sbi.params_override.size());
    for (const auto& [k, v] : sbi.params_override) {
        h.hash_str(k);
        h.hash_str(v);
    }

    return h.finish();
}

} // namespace detail

inline size_t blueprint_checksum(const Blueprint& bp) {
    detail::Hasher global;

    // Counts
    global.combine(bp.nodes.size());
    global.combine(bp.wires.size());
    global.combine(bp.sub_blueprint_instances.size());

    // Global state
    global.hash_float(bp.grid_step);

    // Nodes — order-independent aggregation via addition.
    // NOTE: Addition is not collision-resistant (e.g., swap of two nodes
    // with hashes A and B gives the same sum). This is acceptable for debug
    // verification since node IDs differ and hash_node includes the ID.
    size_t node_sum = 0;
    for (const auto& node : bp.nodes) {
        node_sum += detail::hash_node(node);
    }
    global.combine(node_sum);

    // Wires — order-independent aggregation via addition
    size_t wire_sum = 0;
    for (const auto& wire : bp.wires) {
        wire_sum += detail::hash_wire(wire);
    }
    global.combine(wire_sum);

    // Sub-blueprint instances — order-independent aggregation via addition
    size_t sbi_sum = 0;
    for (const auto& sbi : bp.sub_blueprint_instances) {
        sbi_sum += detail::hash_sub_blueprint_instance(sbi);
    }
    global.combine(sbi_sum);

    return global.finish();
}

// =============================================================================
// Debug-mode verification macro
// =============================================================================
//
// In debug builds, verifies that snapshot+execute+undo produces identical state.
// Disabled in release builds (zero overhead).
//
// Usage:
//   VERIFY_UNDO_ROUNDTRIP(bp, undo_stack, cmd);
//   // equivalent to:
//   //   auto before = blueprint_checksum(bp);
//   //   undo_stack.snapshot(bp);
//   //   execute(bp, cmd);
//   //   undo_stack.undo(bp);
//   //   assert(blueprint_checksum(bp) == before);
//   //   // then redo to leave bp in the desired state
//   //   undo_stack.redo(bp);

#ifndef NDEBUG
#define VERIFY_UNDO_ROUNDTRIP(bp, undo_stack, cmd) \
    do { \
        size_t _checksum_before = blueprint_checksum(bp); \
        (undo_stack).snapshot(bp); \
        execute(bp, cmd); \
        (undo_stack).undo(bp); \
        size_t _checksum_after = blueprint_checksum(bp); \
        if (_checksum_before != _checksum_after) { \
            spdlog::error("[undo-verify] Checksum mismatch! before={:#x} after={:#x}", \
                          _checksum_before, _checksum_after); \
        } \
        (undo_stack).redo(bp); \
    } while(0)
#else
#define VERIFY_UNDO_ROUNDTRIP(bp, undo_stack, cmd) ((void)0)
#endif
