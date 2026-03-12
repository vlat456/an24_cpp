#pragma once

#include "visual/node/widget/widget_base.h"

struct NodeContent;

class SwitchWidget : public Widget {
public:
    SwitchWidget(bool state = false, bool tripped = false);

    void setState(bool s) { state_ = s; }
    void setTripped(bool t) { tripped_ = t; }
    bool state() const { return state_; }
    bool tripped() const { return tripped_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;
    void updateFromContent(const NodeContent& content) override;

    static constexpr float HEIGHT = 20.0f;
    static constexpr float MIN_WIDTH = 40.0f;

private:
    mutable bool state_;
    mutable bool tripped_;
    static constexpr float FONT_SIZE = 11.0f;
    static constexpr float ROUNDING = 4.0f;
};
