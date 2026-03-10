#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "data/pt.h"
#include "visual/spatial_grid.h"

// Forward declaration
class VisualNodeCache;

/// Тип объекта под курсором
enum class HitType {
    None,          ///< Ничего
    Node,          ///< Узел
    Wire,          ///< Провод
    Port,          ///< Порт
    RoutingPoint   ///< Точка изгиба провода
};

/// Результат hit test
struct HitResult {
    HitType type = HitType::None;
    size_t node_index = 0;          ///< Индекс узла
    size_t wire_index = 0;          ///< Индекс провода
    size_t port_index = 0;          ///< Индекс порта
    // BUGFIX [c5a1d8] Removed dead 'is_input' field — port_side is the source of truth
    size_t routing_point_index = 0; ///< Индекс точки изгиба (для HitType::RoutingPoint)

    // Данные для порта (заполняются когда type == Port)
    std::string port_node_id;       ///< ID узла с портом
    std::string port_name;           ///< Имя порта (logical, e.g. "v" for Bus)
    std::string port_wire_id;       ///< [g1h2i3j4] Wire ID for Bus alias ports (empty for normal ports)
    Pt port_position;               ///< Позиция порта
    // BUGFIX [4k9m2x7p] Initialize port_side: was uninitialized causing UB on read when type != Port
    PortSide port_side = PortSide::Input;
};

/// Определить что находится в указанной точке (мировые координаты)
/// group_id filters which nodes/wires are considered ("" = root level)
HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const Viewport& vp, const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid);

/// Hit test для портов
/// group_id filters which nodes are considered ("" = root level)
HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                         const std::string& group_id,
                         const editor_spatial::SpatialGrid& grid);
