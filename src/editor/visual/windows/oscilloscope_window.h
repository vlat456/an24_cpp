#pragma once

#include "editor/visual/oscilloscope_plot.h"
#include "editor/window_system.h"
#include <array>

class OscilloscopeWindow {
public:
    void render(WindowSystem& ws);

private:
    std::array<float, visual::osc::kVisibleSamples> vals_{};
};
