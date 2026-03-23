#include "oscilloscope_window.h"

#include "editor/document.h"
#include "editor/visual/oscilloscope_plot.h"

#include <imgui.h>

void OscilloscopeWindow::render(WindowSystem& ws) {
    if (!ws.showOscilloscope) return;

    if (!ImGui::Begin("Oscilloscope", &ws.showOscilloscope)) {
        ImGui::End();
        return;
    }

    Document* doc = ws.activeDocument();
    if (!doc) {
        ImGui::TextDisabled("No active document");
        ImGui::End();
        return;
    }

    auto channels = ws.oscilloscope.channels();
    if (channels.empty()) {
        ImGui::TextDisabled("Shift+click a wire to add probe");
        ImGui::End();
        return;
    }

    float min_v = 0.0f;
    float max_v = 0.0f;
    visual::osc::compute_range(channels, min_v, max_v);

    std::vector<float> row_heights;
    visual::osc::layout_rows_fill_height(channels, {}, ImGui::GetContentRegionAvail().y, row_heights);

    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& ch = channels[i];
        if (!ch.probe || !ch.samples) continue;
        const float row_h = (i < row_heights.size()) ? row_heights[i] : 56.0f;
        visual::osc::render_channel_plot(*ch.probe, *ch.samples, min_v, max_v, row_h);
        visual::osc::render_stats_row(OscilloscopeModel::compute_stats(*ch.samples));
    }

    ImGui::End();
}
