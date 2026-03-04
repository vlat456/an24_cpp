#pragma once

#include "data/blueprint.h"
#include "data/pt.h"
#include <optional>

/// Результат hit test для routing point
struct RoutingPointHit {
    size_t wire_index;
    size_t routing_point_index;
};

/// Hit test для routing points - найти точку изгиба провода
std::optional<RoutingPointHit> hit_test_routing_point(const Blueprint& bp, Pt world_pos, float radius = 10.0f);
