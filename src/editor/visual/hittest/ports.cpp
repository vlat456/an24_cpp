#include "hittest.h"
#include "visual/spatial/grid.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "layout_constants.h"

HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                         const std::string& group_id,
                         const editor_spatial::SpatialGrid& grid) {
    HitResult result;
    const float PORT_HIT_RADIUS = editor_constants::PORT_HIT_RADIUS;

    std::vector<size_t> candidates;
    candidates.reserve(8);
    grid.query_nodes(world_pos, PORT_HIT_RADIUS, candidates);

    for (size_t node_idx : candidates) {
        if (node_idx >= bp.nodes.size()) continue;
        const auto& node = bp.nodes[node_idx];
        if (node.group_id != group_id) continue;

        auto* visual = cache.getOrCreate(node, bp.wires);
        if (!visual) continue;

        for (size_t port_idx = 0; port_idx < visual->getPortCount(); port_idx++) {
            const auto* port = visual->getPort(port_idx);
            if (!port) break;
            Pt port_pos = port->worldPosition();
            if (editor_math::distance(world_pos, port_pos) <= PORT_HIT_RADIUS) {
                result.type = HitType::Port;
                result.node_index = node_idx;
                result.port_index = port_idx;
                result.port_node_id = node.id;
                result.port_name = port->logicalName();
                result.port_position = port_pos;

                if (port->isAlias()) {
                    result.port_wire_id = port->name();
                }

                result.port_side = port->side();

                return result;
            }
        }
    }
    return result;
}
