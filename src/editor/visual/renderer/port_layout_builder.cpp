#include "visual/renderer/port_layout_builder.h"
#include "visual/node/widget/containers/container.h"
#include "visual/node/widget/containers/row.h"
#include "visual/node/widget/primitives/label.h"
#include "visual/node/widget/primitives/circle.h"
#include "visual/node/widget/primitives/spacer.h"
#include "visual/renderer/render_theme.h"
#include "editor/layout_constants.h"

using namespace editor_constants;

namespace port_layout_builder {

Widget* add_input_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots) {
    auto label = std::make_unique<Label>(port.name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
    float inner_h = label->getPreferredSize(nullptr).y;
    float pad = std::max(0.0f, (PORT_ROW_HEIGHT - inner_h) / 2.0f);
    auto row_container = std::make_unique<Container>(
        std::move(label),
        Edges{PORT_RADIUS + PORT_LABEL_GAP, pad, 0, pad}
    );
    auto* ptr = col.addChild(std::move(row_container));
    slots.push_back({ptr, port.name, true, port.type, 0});
    return ptr;
}

Widget* add_output_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots) {
    auto label = std::make_unique<Label>(port.name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR, TextAlign::Right);
    float inner_h = label->getPreferredSize(nullptr).y;
    float pad = std::max(0.0f, (PORT_ROW_HEIGHT - inner_h) / 2.0f);
    auto row_container = std::make_unique<Container>(
        std::move(label),
        Edges{0, pad, PORT_RADIUS + PORT_LABEL_GAP, pad}
    );
    auto* ptr = col.addChild(std::move(row_container));
    slots.push_back({ptr, port.name, false, port.type, 0});
    return ptr;
}

Widget* build_port_row(Column& target, const PortInfo* left, const PortInfo* right, std::vector<PortSlot>& slots) {
    auto row = std::make_unique<Row>();

    if (left) {
        uint32_t left_color = render_theme::get_port_color(left->type);
        row->addChild(std::make_unique<Container>(
            std::make_unique<Circle>(PORT_RADIUS, left_color),
            Edges{-PORT_RADIUS, 0, PORT_LABEL_GAP, 0}
        ));
        row->addChild(std::make_unique<Label>(left->name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR));
    }

    if (left && right) {
        float half_gap = PORT_MIN_GAP / 2.0f;
        auto gap = std::make_unique<Container>(
            std::make_unique<Spacer>(),
            Edges{half_gap, 0, half_gap, 0}
        );
        gap->setFlexible(true);
        row->addChild(std::move(gap));
    } else {
        row->addChild(std::make_unique<Spacer>());
    }

    if (right) {
        row->addChild(std::make_unique<Label>(right->name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR));
        uint32_t right_color = render_theme::get_port_color(right->type);
        row->addChild(std::make_unique<Container>(
            std::make_unique<Circle>(PORT_RADIUS, right_color),
            Edges{PORT_LABEL_GAP, 0, -PORT_RADIUS, 0}
        ));
    }

    float inner_h = row->getPreferredSize(nullptr).y;
    float pad = std::max(0.0f, (PORT_ROW_HEIGHT - inner_h) / 2.0f);
    auto row_container = std::make_unique<Container>(std::move(row), Edges{0, pad, 0, pad});

    auto* container_ptr = target.addChild(std::move(row_container));

    if (left) {
        slots.push_back({container_ptr, left->name, true, left->type, 0});
    }
    if (right) {
        slots.push_back({container_ptr, right->name, false, right->type, 0});
    }

    return container_ptr;
}

void build_input_column(Column& col, const std::vector<PortInfo>& inputs, std::vector<PortSlot>& slots) {
    for (const auto& port : inputs) {
        add_input_label_to_column(col, port, slots);
    }
}

void build_output_column(Column& col, const std::vector<PortInfo>& outputs, std::vector<PortSlot>& slots) {
    for (const auto& port : outputs) {
        add_output_label_to_column(col, port, slots);
    }
}

}
