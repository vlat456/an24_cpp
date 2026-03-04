#pragma once

#include "data/pt.h"
#include <optional>

/// Состояние перетаскивания
enum class Dragging {
    None,           ///< Ничего не перетаскивается
    Node,           ///< Перетаскивается узел
    RoutingPoint    ///< Перетаскивается точка изгиба провода
};

/// Interaction state - состояние взаимодействия с канвой
///
/// Хранит selection, drag state, panning state.
struct Interaction {
    /// Выделенный узел
    std::optional<size_t> selected_node;

    /// Выделенный провод
    std::optional<size_t> selected_wire;

    /// Текущее перетаскивание
    Dragging dragging = Dragging::None;

    /// Режим панорамирования
    bool panning = false;

    /// Якорь перетаскивания (накопление дробных смещений)
    Pt drag_anchor;

    /// Индекс провода для RoutingPoint
    size_t routing_point_wire = 0;

    /// Индекс точки изгиба
    size_t routing_point_index = 0;

    Interaction() = default;

    /// Очистить выделение
    void clear_selection() {
        selected_node.reset();
        selected_wire.reset();
    }

    /// Начать перетаскивание узла
    void start_drag_node(Pt anchor) {
        dragging = Dragging::Node;
        drag_anchor = anchor;
    }

    /// Начать перетаскивание точки изгиба провода
    void start_drag_routing_point(size_t wire_idx, size_t point_idx) {
        dragging = Dragging::RoutingPoint;
        routing_point_wire = wire_idx;
        routing_point_index = point_idx;
    }

    /// Обновить якорь перетаскивания (добавить delta)
    void update_drag_anchor(Pt delta) {
        drag_anchor = drag_anchor + delta;
    }

    /// Закончить перетаскивание
    void end_drag() {
        dragging = Dragging::None;
    }

    /// Установить режим панорамирования
    void set_panning(bool value) {
        panning = value;
    }
};
