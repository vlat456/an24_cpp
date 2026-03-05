#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "data/pt.h"

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
    bool is_input = false;          ///< Входной/выходной порт
    size_t routing_point_index = 0; ///< Индекс точки изгиба (для HitType::RoutingPoint)

    // Данные для порта (заполняются когда type == Port)
    std::string port_node_id;       ///< ID узла с портом
    std::string port_name;           ///< Имя порта
    Pt port_position;               ///< Позиция порта
    PortSide port_side;             ///< Сторона порта
};

/// Определить что находится в указанной точке (мировые координаты)
/// [h1a2b3c4] Overload accepting VisualNodeCache to avoid fresh visual creation.
HitResult hit_test(const Blueprint& bp, const VisualNodeCache& cache, Pt world_pos, const Viewport& vp);

/// Legacy overload without cache (creates fresh visuals – slow path)
HitResult hit_test(const Blueprint& bp, Pt world_pos, const Viewport& vp);

/// Hit test для портов (включая кэш визуальных узлов)
HitResult hit_test_ports(const Blueprint& bp, const VisualNodeCache& cache, Pt world_pos);
