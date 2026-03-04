#pragma once

#include "pt.h"
#include "port.h"
#include <string>
#include <vector>

/// Вид узла (для рендеринга)
enum class NodeKind {
    Node,       ///< Обычный компонент (батарея, насос, etc.)
    Bus,        ///< Шина/мультиплексор - маленький квадрат
    Ref         ///< Reference node (ground, voltage source)
};

/// Тип содержимого узла (пока простой enum)
enum class NodeContentType {
    None,
    Gauge,     ///< Измерительный прибор
    Switch,    ///< Переключатель
    Value,     ///< Отображаемое значение
    Text       ///< Текст
};

/// Содержимое узла (пока placeholder)
struct NodeContent {
    NodeContentType type = NodeContentType::None;
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
};

/// Узел в схеме - компонент (батарея, насос, и т.д.)
struct Node {
    std::string id;          ///< Уникальный ID
    std::string name;        ///< Отображаемое имя
    std::string type_name;   ///< Тип (Battery, Pump, Bus, etc.)
    NodeKind kind = NodeKind::Node;  ///< Вид узла для рендеринга

    Pt pos;                  ///< Позиция (верхний левый угол)
    Pt size;                 ///< Размеры (ширина × высота)

    std::vector<Port> inputs;    ///< Входные порты
    std::vector<Port> outputs;   ///< Выходные порты

    NodeContent node_content;  ///< Содержимое для отображения

    Node()
        : id()
        , name()
        , type_name()
        , kind(NodeKind::Node)
        , pos(Pt::zero())
        , size(120.0f, 80.0f)
        , inputs()
        , outputs()
        , node_content()
    {}

    /// fluent: задать позицию
    Node& at(float x, float y) {
        pos = Pt(x, y);
        return *this;
    }

    /// fluent: задать размеры
    Node& size_wh(float w, float h) {
        size = Pt(w, h);
        return *this;
    }

    /// fluent: добавить входной порт
    Node& input(const char* name_) {
        inputs.emplace_back(name_, PortSide::Input);
        return *this;
    }

    /// fluent: добавить выходной порт
    Node& output(const char* name_) {
        outputs.emplace_back(name_, PortSide::Output);
        return *this;
    }

    /// fluent: задать содержимое
    Node& with_content(NodeContent c) {
        node_content = std::move(c);
        return *this;
    }
};
