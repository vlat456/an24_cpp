#pragma once

#include "data/pt.h"
#include <optional>
#include <vector>
#include <algorithm>

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
    /// Выделенные узлы (для множественного выделения)
    std::vector<size_t> selected_nodes;

    /// Выделенный провод (пока один)
    std::optional<size_t> selected_wire;

    /// Текущее перетаскивание
    Dragging dragging = Dragging::None;

    /// Режим панорамирования
    bool panning = false;

    /// Marquee selection (Alt/ Cmd drag)
    bool marquee_selecting = false;
    Pt marquee_start;
    Pt marquee_end;

    /// Якорь перетаскивания (накопление дробных смещений)
    Pt drag_anchor;

    /// Индекс провода для RoutingPoint
    size_t routing_point_wire = 0;

    /// Индекс точки изгиба
    size_t routing_point_index = 0;

    /// Смещения выделенных узлов относительно drag_anchor (для мульти-выделения)
    std::vector<Pt> drag_node_offsets;

    Interaction() = default;

    /// Очистить выделение
    void clear_selection() {
        selected_nodes.clear();
        selected_wire.reset();
    }

    /// Добавить узел к выделению
    void add_node_selection(size_t idx) {
        if (std::find(selected_nodes.begin(), selected_nodes.end(), idx) == selected_nodes.end()) {
            selected_nodes.push_back(idx);
        }
    }

    /// Проверить, выделен ли узел
    bool is_node_selected(size_t idx) const {
        return std::find(selected_nodes.begin(), selected_nodes.end(), idx) != selected_nodes.end();
    }

    /// Получить первый выделенный узел
    std::optional<size_t> first_selected_node() const {
        if (selected_nodes.empty()) return std::nullopt;
        return selected_nodes[0];
    }

    /// Начать перетаскивание узла (anchor = позиция первого выделенного узла)
    void start_drag_node(Pt node_pos) {
        dragging = Dragging::Node;
        drag_anchor = node_pos;
    }

    /// Начать перетаскивание точки изгиба провода
    void start_drag_routing_point(size_t wire_idx, size_t point_idx, Pt rp_pos) {
        dragging = Dragging::RoutingPoint;
        routing_point_wire = wire_idx;
        routing_point_index = point_idx;
        drag_anchor = rp_pos;
    }

    /// Обновить якорь перетаскивания (добавить delta)
    void update_drag_anchor(Pt delta) {
        drag_anchor = drag_anchor + delta;
    }

    /// Закончить перетаскивание
    void end_drag() {
        dragging = Dragging::None;
    }

    /// Сбросить drag (без изменения panning)
    void clear_drag() {
        dragging = Dragging::None;
    }

    /// Установить режим панорамирования
    void set_panning(bool value) {
        panning = value;
    }
};
