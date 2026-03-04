#include "wires/hittest.h"
#include "trigonometry.h"

// TDD Green: Реализация hit_test_routing_point
// Ищет routing point в проводах, возвращает индекс провода и точки
std::optional<RoutingPointHit> hit_test_routing_point(const Blueprint& bp, Pt world_pos, float radius) {
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& wire = bp.wires[wire_idx];
        for (size_t rp_idx = 0; rp_idx < wire.routing_points.size(); rp_idx++) {
            const Pt& rp = wire.routing_points[rp_idx];
            if (editor_math::distance(world_pos, rp) <= radius) {
                return RoutingPointHit{wire_idx, rp_idx};
            }
        }
    }
    return std::nullopt;
}
