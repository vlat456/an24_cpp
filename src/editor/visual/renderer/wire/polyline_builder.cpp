#include "polyline_builder.h"
#include "visual/trigonometry.h"

namespace polyline_builder {

std::vector<std::vector<Pt>> build_wire_polylines(
    const Blueprint& bp,
    const std::string& group_id,
    VisualNodeCache& cache) {

    std::vector<std::vector<Pt>> polylines;
    polylines.reserve(bp.wires.size());

    for (const auto& w : bp.wires) {
        auto endpoints = editor_math::resolve_wire_endpoints(w, bp, group_id, cache);
        if (!endpoints) {
            polylines.push_back({});
            continue;
        }
        auto [start_pos, end_pos] = *endpoints;

        std::vector<Pt> poly;
        poly.push_back(start_pos);
        poly.insert(poly.end(), w.routing_points.begin(), w.routing_points.end());
        poly.push_back(end_pos);

        polylines.push_back(std::move(poly));
    }

    return polylines;
}

}
