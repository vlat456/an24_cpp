#pragma once

#include "visual/node/widget/widget_base.h"

struct NodeContent;

class VerticalToggleWidget : public Widget {
public:
    VerticalToggleWidget(bool state = false, bool tripped = false);

    void setState(bool s) { state_ = s; }
    void setTripped(bool t) { tripped_ = t; }
    bool state() const { return state_; }
    bool tripped() const { return tripped_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;
    void updateFromContent(const NodeContent& content) override;

    static constexpr float WIDTH = 16.0f;
    static constexpr float HEIGHT = 50.0f;
    static constexpr float TRACK_WIDTH = 6.0f;
    static constexpr float HANDLE_SIZE = 12.0f;

private:
    mutable bool state_;
    mutable bool tripped_;
    static constexpr float ROUNDING = 2.0f;
};
