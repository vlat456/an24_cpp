#pragma once
#include "visual/widget.h"

namespace visual {

/// RoutingPoint is a child of Wire. Clickable for Grid tracking and drag.
class RoutingPoint : public Widget {
public:
    explicit RoutingPoint(Pt pos) { local_pos_ = pos; }

    bool isClickable() const override { return true; }
};

} // namespace visual
