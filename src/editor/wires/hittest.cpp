#include "wires/hittest.h"
#include "visual/trigonometry.h"

// BUGFIX [3f7b9c] Added group_id filter to avoid matching routing points from other blueprint groups
std::optional<RoutingPointHit> hit_test_routing_point(const Blueprint& bp, Pt world_pos,
                                                       const std::string& group_id,
                                                       float radius) {
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& wire = bp.wires[wire_idx];

        // Filter: only consider wires whose endpoints belong to this group
        const Node* sn = bp.find_node(wire.start.node_id.c_str());
        const Node* en = bp.find_node(wire.end.node_id.c_str());
        if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

        for (size_t rp_idx = 0; rp_idx < wire.routing_points.size(); rp_idx++) {
            const Pt& rp = wire.routing_points[rp_idx];
            if (editor_math::distance(world_pos, rp) <= radius) {
                return RoutingPointHit{wire_idx, rp_idx};
            }
        }
    }
    return std::nullopt;
}
