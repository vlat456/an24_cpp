#include "hittest.h"
#include "visual/spatial/grid.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "layout_constants.h"

namespace {

bool check_resize_handles(const VisualNode* visual, size_t node_index,
                          Pt world_pos, HitResult& result) {
    if (!visual->isResizable()) return false;

    Pt pos = visual->getPosition();
    Pt sz = visual->getSize();
    float r = editor_constants::RESIZE_HANDLE_HIT_RADIUS;

    struct Corner { Pt pt; ResizeCorner corner; };
    Corner corners[] = {
        { Pt(pos.x, pos.y),                     ResizeCorner::TopLeft },
        { Pt(pos.x + sz.x, pos.y),              ResizeCorner::TopRight },
        { Pt(pos.x, pos.y + sz.y),              ResizeCorner::BottomLeft },
        { Pt(pos.x + sz.x, pos.y + sz.y),       ResizeCorner::BottomRight },
    };

    for (const auto& c : corners) {
        if (editor_math::distance(world_pos, c.pt) <= r) {
            result.type = HitType::ResizeHandle;
            result.node_index = node_index;
            result.resize_corner = c.corner;
            return true;
        }
    }
    return false;
}

}

HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid) {
    HitResult result;

    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        grid.query_nodes(world_pos, editor_constants::RESIZE_HANDLE_HIT_RADIUS, candidates);

        for (size_t i : candidates) {
            if (i >= bp.nodes.size()) continue;
            const auto& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            auto* visual = cache.getOrCreate(n, bp.wires);
            if (check_resize_handles(visual, i, world_pos, result))
                return result;
        }

        for (size_t i : candidates) {
            if (i >= bp.nodes.size()) continue;
            const auto& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            auto* visual = cache.getOrCreate(n, bp.wires);
            if (visual->containsPoint(world_pos)) {
                result.type = HitType::Node;
                result.node_index = i;
                return result;
            }
        }
    }

    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        float margin = std::max(editor_constants::ROUTING_POINT_HIT_RADIUS,
                                editor_constants::WIRE_SEGMENT_HIT_TOLERANCE);
        grid.query_wires(world_pos, margin, candidates);

        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;
            for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
                if (editor_math::distance(world_pos, w.routing_points[rp_idx])
                        <= editor_constants::ROUTING_POINT_HIT_RADIUS) {
                    result.type = HitType::RoutingPoint;
                    result.wire_index = wire_idx;
                    result.routing_point_index = rp_idx;
                    return result;
                }
            }
        }

        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            Pt prev = start_pos;
            bool hit_found = false;
            for (const auto& rp : w.routing_points) {
                if (editor_math::distance_to_segment(world_pos, prev, rp)
                        < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE) {
                    hit_found = true; break;
                }
                prev = rp;
            }
            if (!hit_found && editor_math::distance_to_segment(world_pos, prev, end_pos)
                    < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE)
                hit_found = true;

            if (hit_found) {
                result.type = HitType::Wire;
                result.wire_index = wire_idx;
                return result;
            }
        }
    }

    return result;
}
