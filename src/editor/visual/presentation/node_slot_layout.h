#pragma once

#include "editor/visual/presentation/node_presentation.h"
#include "ui/math/pt.h"
#include <vector>

namespace editor::presentation {

enum class NodeSlot {
    Header,
    Body,
    LeftPorts,
    RightPorts,
    TopPorts,
    BottomPorts,
    Overlay,
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct SlotAssignment {
    NodeSlot slot = NodeSlot::Body;
    Rect bounds;
};

struct FragmentPlacement {
    ui::InternedId element_id;
    Rect bounds;
};

struct NodeSlotLayout {
    Rect node_bounds;
    std::vector<SlotAssignment> slots;
    std::vector<FragmentPlacement> placements;
};

struct NodeSlotLayoutStyle {
    float min_width = 120.0f;
    float min_height = 80.0f;
    float header_height = 24.0f;
    float side_strip_width = 20.0f;
    float top_strip_height = 0.0f;
    float bottom_strip_height = 0.0f;
    float body_padding = 8.0f;
    float overlay_inset = 0.0f;
};

NodeSlotLayout layout_node_presentation(const NodePresentation& presentation,
                                        ui::Pt node_size,
                                        const NodeSlotLayoutStyle& style = {});

} // namespace editor::presentation
