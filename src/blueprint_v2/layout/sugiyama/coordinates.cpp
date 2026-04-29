#include "coordinates.h"

namespace bp2::layout::sugiyama {

namespace {

/// Lookup table for node dimensions by id.
struct NodeSizeMap {
    float lookup_width(core::InternedId id, float fallback) const {
        auto it = map.find(id);
        return it != map.end() ? it->second.width : fallback;
    }

    float lookup_height(core::InternedId id, float fallback) const {
        auto it = map.find(id);
        return it != map.end() ? it->second.height : fallback;
    }

    struct Size { float width; float height; };
    std::unordered_map<core::InternedId, Size> map;
};

NodeSizeMap build_size_map(const Graph& graph) {
    NodeSizeMap sm;
    for (const auto& n : graph.nodes) {
        sm.map[n.id] = {n.width, n.height};
    }
    return sm;
}

} // namespace

std::unordered_map<core::InternedId, NodePosition> assign_coordinates(
    const Graph& graph,
    const Layering& layering,
    const Spacing& spacing) {

    std::unordered_map<core::InternedId, NodePosition> result;
    if (layering.layers.empty()) return result;

    const auto sizes = build_size_map(graph);

    // First pass: compute total height of each layer.
    struct LayerInfo {
        float total_height = 0.0f;  ///< Sum of all node heights + gaps.
        float max_height = 0.0f;    ///< Tallest node in this layer.
    };
    std::vector<LayerInfo> layer_infos(layering.layers.size());

    for (size_t li = 0; li < layering.layers.size(); ++li) {
        const auto& layer = layering.layers[li];
        for (size_t pos = 0; pos < layer.size(); ++pos) {
            float h = sizes.lookup_height(layer[pos], 80.0f);
            layer_infos[li].max_height = std::max(layer_infos[li].max_height, h);
            layer_infos[li].total_height += h;
            if (pos + 1 < layer.size()) {
                layer_infos[li].total_height += spacing.vertical;
            }
        }
    }

    // Second pass: assign coordinates, centering each layer vertically
    // around the tallest layer's midpoint.
    float max_layer_height = 0.0f;
    for (const auto& info : layer_infos) {
        max_layer_height = std::max(max_layer_height, info.total_height);
    }

    for (size_t layer_idx = 0; layer_idx < layering.layers.size(); ++layer_idx) {
        const auto& layer = layering.layers[layer_idx];
        float x = spacing.margin_x + static_cast<float>(layer_idx) * spacing.horizontal;

        // Center this layer vertically relative to the tallest layer.
        float offset_y = (max_layer_height - layer_infos[layer_idx].total_height) * 0.5f;
        float y = spacing.margin_y + offset_y;

        for (size_t pos = 0; pos < layer.size(); ++pos) {
            core::InternedId id = layer[pos];
            result[id] = {x, y};

            float h = sizes.lookup_height(id, 80.0f);
            y += h + spacing.vertical;
        }
    }

    return result;
}

} // namespace bp2::layout::sugiyama
