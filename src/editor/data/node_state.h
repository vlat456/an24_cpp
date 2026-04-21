#pragma once

#include "ui/core/interned_id.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ui {
class StringInterner;
}

namespace editor {

/// Optional per-node custom color (RGBA, 0.0–1.0).
struct NodeColor {
    float r = 0.5f;
    float g = 0.5f;
    float b = 0.5f;
    float a = 1.0f;

    bool operator==(const NodeColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    /// Convert to ImGui uint32 ABGR format (0xAABBGGRR).
    uint32_t to_uint32() const {
        auto clamp01 = [](float v) -> float {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };

        const uint8_t ri = static_cast<uint8_t>(clamp01(r) * 255.0f + 0.5f);
        const uint8_t gi = static_cast<uint8_t>(clamp01(g) * 255.0f + 0.5f);
        const uint8_t bi = static_cast<uint8_t>(clamp01(b) * 255.0f + 0.5f);
        const uint8_t ai = static_cast<uint8_t>(clamp01(a) * 255.0f + 0.5f);
        return (uint32_t(ai) << 24) | (uint32_t(bi) << 16) | (uint32_t(gi) << 8) | uint32_t(ri);
    }
};

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
using SessionNodeAppearanceStore = std::unordered_map<NodeInstanceKey, NodeColor, NodeInstanceKeyHash>;

/// Construct a NodeInstanceKey from a pre-built typed instance path.
/// No string parsing involved — the path is already in canonical InternedId form.
inline NodeInstanceKey make_node_instance_key(std::span<const ui::InternedId> instance_path,
                                               ui::InternedId local_node_id) {
    NodeInstanceKey key;
    key.instance_path.assign(instance_path.begin(), instance_path.end());
    key.local_node_id = local_node_id;
    return key;
}

/// Construct a NodeInstanceKey from a single-segment scope string (window scope key).
/// IMPORTANT: Only handles single-segment scope keys (e.g. "group_5").
/// For multi-segment paths, use the span<const InternedId> overload.
/// For root scope (empty string), instance_path is empty.
/// If scope_key is non-empty but not in the interner, silently treated as root scope.
inline NodeInstanceKey make_node_instance_key(ui::StringInterner& interner,
                                               std::string_view scope_key,
                                               ui::InternedId local_node_id) {
    NodeInstanceKey key;
    key.local_node_id = local_node_id;
    if (!scope_key.empty()) {
        const ui::InternedId scope_iid = interner.lookup(scope_key);
        if (!scope_iid.empty()) {
            key.instance_path.push_back(scope_iid);
        }
    }
    return key;
}

inline std::optional<NodeColor> lookup_node_color(const SessionNodeAppearanceStore& store,
                                                  const NodeInstanceKey& key) {
    const auto it = store.find(key);
    if (it == store.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace editor
