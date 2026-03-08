#pragma once

#include "data/blueprint.h"
#include "data/pt.h"
#include <optional>
#include <string>

/// Результат hit test для routing point
struct RoutingPointHit {
    size_t wire_index;
    size_t routing_point_index;
};

/// Hit test для routing points - найти точку изгиба провода
/// BUGFIX [3f7b9c] Added group_id filter — was matching routing points from other groups
std::optional<RoutingPointHit> hit_test_routing_point(const Blueprint& bp, Pt world_pos,
                                                       const std::string& group_id = "",
                                                       float radius = 10.0f);
