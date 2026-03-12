#pragma once

#include "data/blueprint.h"
#include "data/pt.h"
#include "input/input_types.h"
#include "visual/spatial/grid.h"

enum class HitType {
    None,
    Node,
    Wire,
    Port,
    RoutingPoint,
    ResizeHandle
};

struct HitResult {
    HitType type = HitType::None;
    size_t node_index = 0;
    size_t wire_index = 0;
    size_t port_index = 0;
    size_t routing_point_index = 0;
    ResizeCorner resize_corner = ResizeCorner::BottomRight;

    std::string port_node_id;
    std::string port_name;
    std::string port_wire_id;
    Pt port_position;
    PortSide port_side = PortSide::Input;
};

HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid);

HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                         const std::string& group_id,
                         const editor_spatial::SpatialGrid& grid);
