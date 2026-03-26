#include "oscilloscope_plot.h"

#include <algorithm>

namespace visual::osc {

void render_channel_plot(const OscilloscopeProbe& probe,
                         const std::deque<float>& samples,
                         float min_v,
                         float max_v,
                         float row_h,
                         float width) {
    constexpr int kScopeWindowSamples = 300;
    const std::string title = "##" + probe.wire_id;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, probe.color);
    ImGui::TextUnformatted(probe.label.c_str());
    if (samples.empty()) {
        std::vector<float> vals(static_cast<size_t>(kScopeWindowSamples), 0.0f);
        ImGui::PlotLines(title.c_str(), vals.data(), kScopeWindowSamples, 0, "", min_v, max_v, ImVec2(width, row_h));
    } else {
        std::vector<float> vals(static_cast<size_t>(kScopeWindowSamples), 0.0f);
        const size_t copy_n = std::min(samples.size(), static_cast<size_t>(kScopeWindowSamples));
        auto src_begin = samples.end() - static_cast<std::ptrdiff_t>(copy_n);
        std::copy(src_begin, samples.end(), vals.end() - static_cast<std::ptrdiff_t>(copy_n));
        ImGui::PlotLines(title.c_str(), vals.data(), kScopeWindowSamples, 0, "", min_v, max_v, ImVec2(width, row_h));
    }
    ImGui::PopStyleColor();
}

void compute_range(const std::vector<OscilloscopeModel::ChannelView>& channels,
                   float& out_min_v,
                   float& out_max_v) {
    bool has_val = false;
    float min_v = 0.0f;
    float max_v = 0.0f;
    for (const auto& ch : channels) {
        if (!ch.samples || ch.samples->empty()) continue;
        for (float v : *ch.samples) {
            if (!has_val) {
                min_v = max_v = v;
                has_val = true;
            } else {
                min_v = std::min(min_v, v);
                max_v = std::max(max_v, v);
            }
        }
    }

    if (!has_val) {
        out_min_v = -1.0f;
        out_max_v = 1.0f;
        return;
    }

    if (max_v - min_v < 1e-3f) {
        min_v -= 1.0f;
        max_v += 1.0f;
    }
    out_min_v = min_v;
    out_max_v = max_v;
}

void draw_probe_marker(ImDrawList* draw_list,
                       const ui::Pt& screen_pos,
                       uint32_t color,
                       float radius) {
    draw_list->AddCircleFilled(ImVec2(screen_pos.x, screen_pos.y), radius, color, 24);
    draw_list->AddCircle(ImVec2(screen_pos.x, screen_pos.y), radius + 2.0f, IM_COL32(20, 20, 20, 220), 24, 2.0f);
}

void render_stats_row(const OscilloscopeModel::SampleStats& stats) {
    if (!stats.has_value) {
        ImGui::TextDisabled("min --   max --   last --   Tu --");
        return;
    }
    if (stats.has_tu) {
        ImGui::Text("min %.3f   max %.3f   last %.3f   Tu %.3fs",
                    stats.min_v, stats.max_v, stats.last_v, stats.tu_sec);
    } else {
        ImGui::Text("min %.3f   max %.3f   last %.3f   Tu --",
                    stats.min_v, stats.max_v, stats.last_v);
    }
}

} // namespace visual::osc
