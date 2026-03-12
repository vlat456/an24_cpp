#pragma once
#include "data/pt.h"
#include "data/blueprint.h"
#include "visual/node/visual_node_cache.h"
#include "layout_constants.h"
#include "visual/trigonometry.h"
#include <unordered_map>
#include <vector>
#include <cmath>

namespace editor_spatial {

constexpr float CELL_SIZE = 64.0f;  // Must be > PORT_HIT_RADIUS * 2

namespace detail {
/// Append idx to vec only if not already present.  O(n) is fine for small candidate sets.
inline void push_unique(std::vector<size_t>& vec, size_t idx) {
    for (size_t x : vec) { if (x == idx) return; }
    vec.push_back(idx);
}
} // namespace detail

inline int cell_coord(float world) {
    return (int)std::floor(world / CELL_SIZE);
}

// Compact key: encode (cx, cy) into a single int64
inline int64_t cell_key(int cx, int cy) {
    return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cy;
}

struct SpatialCell {
    std::vector<size_t> node_indices;
    std::vector<size_t> wire_indices;  // wires with segments OR routing_points here
};

class SpatialGrid {
public:
    void clear() { cells_.clear(); }

    // Build the grid from scratch. Call when blueprint changes structurally
    // (node added/removed, wire added/removed, node moved).
    void rebuild(const Blueprint& bp, VisualNodeCache& cache,
                 const std::string& group_id) {
        clear();

        // Register nodes
        for (size_t i = 0; i < bp.nodes.size(); i++) {
            const Node& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            insert_node(i, n.pos, Pt(n.pos.x + n.size.x, n.pos.y + n.size.y));
        }

        // Register wires (segments + routing points)
        for (size_t i = 0; i < bp.wires.size(); i++) {
            const Wire& w = bp.wires[i];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en) continue;
            if (sn->group_id != group_id || en->group_id != group_id) continue;

            // Get resolved endpoints from cache (handles Bus alias ports)
            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            // Rasterize all segments (start→rp[0]→rp[1]→...→end)
            Pt prev = start_pos;
            for (const Pt& rp : w.routing_points) {
                insert_segment(i, prev, rp);
                prev = rp;
            }
            insert_segment(i, prev, end_pos);
        }
    }

    // Query: return candidate node indices covering the given point (+tolerance margin).
    // Returns set of unique node indices from 3×3 neighborhood (for port hit radius).
    void query_nodes(Pt world_pos, float margin,
                     std::vector<size_t>& out_nodes) const {
        int cx0 = cell_coord(world_pos.x - margin);
        int cy0 = cell_coord(world_pos.y - margin);
        int cx1 = cell_coord(world_pos.x + margin);
        int cy1 = cell_coord(world_pos.y + margin);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(cell_key(cx, cy));
                if (it == cells_.end()) continue;
                for (size_t idx : it->second.node_indices)
                    detail::push_unique(out_nodes, idx);
            }
        }
    }

    // Query: return candidate wire indices for the given point (+tolerance margin).
    void query_wires(Pt world_pos, float margin,
                     std::vector<size_t>& out_wires) const {
        int cx0 = cell_coord(world_pos.x - margin);
        int cy0 = cell_coord(world_pos.y - margin);
        int cx1 = cell_coord(world_pos.x + margin);
        int cy1 = cell_coord(world_pos.y + margin);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(cell_key(cx, cy));
                if (it == cells_.end()) continue;
                for (size_t idx : it->second.wire_indices)
                    detail::push_unique(out_wires, idx);
            }
        }
    }

private:
    std::unordered_map<int64_t, SpatialCell> cells_;

    SpatialCell& get_or_create(int cx, int cy) {
        return cells_[cell_key(cx, cy)];
    }

    // Insert node bounding box into all covered cells (expanded by PORT_HIT_RADIUS
    // so that port queries from neighboring cells still find it).
    void insert_node(size_t idx, Pt world_min, Pt world_max) {
        float margin = editor_constants::PORT_HIT_RADIUS;
        int cx0 = cell_coord(world_min.x - margin);
        int cy0 = cell_coord(world_min.y - margin);
        int cx1 = cell_coord(world_max.x + margin);
        int cy1 = cell_coord(world_max.y + margin);
        for (int cx = cx0; cx <= cx1; cx++)
            for (int cy = cy0; cy <= cy1; cy++)
                get_or_create(cx, cy).node_indices.push_back(idx);
    }

    // Rasterize one wire segment into all covered cells (expanded by hit tolerance).
    void insert_segment(size_t wire_idx, Pt a, Pt b) {
        float tol = editor_constants::WIRE_SEGMENT_HIT_TOLERANCE;
        float min_x = std::min(a.x, b.x) - tol;
        float max_x = std::max(a.x, b.x) + tol;
        float min_y = std::min(a.y, b.y) - tol;
        float max_y = std::max(a.y, b.y) + tol;

        int cx0 = cell_coord(min_x), cy0 = cell_coord(min_y);
        int cx1 = cell_coord(max_x), cy1 = cell_coord(max_y);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                detail::push_unique(get_or_create(cx, cy).wire_indices, wire_idx);
            }
        }
    }
};

} // namespace editor_spatial
