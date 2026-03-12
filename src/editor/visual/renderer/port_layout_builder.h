#pragma once

#include "visual/node/widget/widget_base.h"
#include "visual/node/widget/containers/column.h"
#include "json_parser/json_parser.h"
#include <memory>
#include <string>
#include <vector>

class Node;

namespace port_layout_builder {

struct PortSlot {
    Widget* row_container = nullptr;
    std::string name;
    bool is_left = true;
    PortType type = PortType::Any;
    float parent_y_offset = 0.0f;
};

struct PortInfo {
    std::string name;
    PortType type;
};

Widget* add_input_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots);

Widget* add_output_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots);

Widget* build_port_row(Column& target, const PortInfo* left, const PortInfo* right, std::vector<PortSlot>& slots);

void build_input_column(Column& col, const std::vector<PortInfo>& inputs, std::vector<PortSlot>& slots);

void build_output_column(Column& col, const std::vector<PortInfo>& outputs, std::vector<PortSlot>& slots);

}
