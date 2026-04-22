#pragma once

#include "blueprint_v2/blueprint/node_color.h"
#include "ui/core/interned_id.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace editor {

using NodeColor = bp2::NodeColor;

/// Runtime/editor identity for a node instance inside a possibly nested instance path.
struct NodeInstanceKey {
    std::vector<ui::InternedId> instance_path;
    ui::InternedId local_node_id;

    bool operator==(const NodeInstanceKey& other) const {
        return local_node_id == other.local_node_id && instance_path == other.instance_path;
    }
};

struct NodeInstanceKeyHash {
    size_t operator()(const NodeInstanceKey& key) const noexcept {
        size_t seed = std::hash<uint32_t>{}(key.local_node_id.raw());
        for (const ui::InternedId segment : key.instance_path) {
            seed ^= std::hash<uint32_t>{}(segment.raw()) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }
        return seed;
    }
};

struct ScalarNodeRuntimeState {
    float value = 0.0f;
};

struct BoolNodeRuntimeState {
    bool state = false;
};

struct BoolTrippedNodeRuntimeState {
    bool state = false;
    bool tripped = false;
};

struct DiscreteNodeRuntimeState {
    int position = 0;
};

using RuntimeNodeState = std::variant<
    std::monostate,
    ScalarNodeRuntimeState,
    BoolNodeRuntimeState,
    BoolTrippedNodeRuntimeState,
    DiscreteNodeRuntimeState>;

using RuntimeNodeStateStore = std::unordered_map<NodeInstanceKey, RuntimeNodeState, NodeInstanceKeyHash>;
/// Construct a NodeInstanceKey from a pre-built typed instance path.
/// No string parsing involved — the path is already in canonical InternedId form.
inline NodeInstanceKey make_node_instance_key(std::span<const ui::InternedId> instance_path,
                                              ui::InternedId local_node_id) {
    NodeInstanceKey key;
    key.instance_path.assign(instance_path.begin(), instance_path.end());
    key.local_node_id = local_node_id;
    return key;
}

} // namespace editor
