#include "polyline_builder.h"
#include "visual/trigonometry.h"
#include "data/node.h"

namespace polyline_builder {

std::vector<std::vector<Pt>> build_wire_polylines(
    const Blueprint& bp,
    const std::string& group_id,
    VisualNodeCache& cache) {
    
    std::unordered_map<std::string, const Node*> node_map;
    node_map.reserve(bp.nodes.size());
    for (const auto& n : bp.nodes)
        node_map[n.id] = &n;

    std::vector<std::vector<Pt>> polylines;
    polylines.reserve(bp.wires.size());

    for (const auto& w : bp.wires) {
        auto it_s = node_map.find(w.start.node_id);
        auto it_e = node_map.find(w.end.node_id);
        const Node* start_node = (it_s != node_map.end()) ? it_s->second : nullptr;
        const Node* end_node   = (it_e != node_map.end()) ? it_e->second : nullptr;

        if (!start_node || !end_node) {
            polylines.push_back({});
            continue;
        }

        if (start_node->group_id != group_id || end_node->group_id != group_id) {
            polylines.push_back({});
            continue;
        }

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(),
                                                       bp.wires, w.id.c_str(), cache);
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(),
                                                     bp.wires, w.id.c_str(), cache);

        std::vector<Pt> poly;
        poly.push_back(start_pos);
        poly.insert(poly.end(), w.routing_points.begin(), w.routing_points.end());
        poly.push_back(end_pos);

        polylines.push_back(std::move(poly));
    }

    return polylines;
}

}
