#pragma once

#include "data/blueprint.h"
#include "viewport.h"
#include "data/pt.h"

/// Тип объекта под курсором
enum class HitType {
    None,   ///< Ничего
    Node,   ///< Узел
    Wire,   ///< Провод
    Port    ///< Порт
};

/// Результат hit test
struct HitResult {
    HitType type = HitType::None;
    size_t node_index = 0;      ///< Индекс узла
    size_t wire_index = 0;      ///< Индекс провода
    size_t port_index = 0;      ///< Индекс порта
    bool is_input = false;     ///< Входной/выходной порт
};

/// Определить что находится в указанной точке (мировые координаты)
HitResult hit_test(const Blueprint& bp, Pt world_pos, const Viewport& vp);
