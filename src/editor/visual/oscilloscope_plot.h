#pragma once

#include "editor/oscilloscope.h"
#include "ui/math/pt.h"

#include <imgui.h>

namespace visual::osc {

struct PlotStyle {
    float min_row_h = 40.0f;
    float default_row_h = 56.0f;
};

void render_channel_plot(const OscilloscopeProbe& probe,
                         const std::deque<float>& samples,
                         float min_v,
                         float max_v,
                         float row_h,
                         float width = -1.0f);

void compute_range(const std::vector<OscilloscopeModel::ChannelView>& channels,
                   float& out_min_v,
                   float& out_max_v);

void layout_rows_fill_height(const std::vector<OscilloscopeModel::ChannelView>& channels,
                             const PlotStyle& style,
                             float avail_h,
                             std::vector<float>& out_row_heights);

void draw_probe_marker(ImDrawList* draw_list,
                       const ui::Pt& screen_pos,
                       uint32_t color,
                       float radius = 7.0f);

void render_stats_row(const OscilloscopeModel::SampleStats& stats);

} // namespace visual::osc
