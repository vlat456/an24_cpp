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

    Document const* doc = ws.activeDocument();
    if (!doc) {
        ImGui::TextDisabled("No active document");
        ImGui::End();
        return;
    }

    auto channels = ws.oscilloscope.channels_for(doc->id());
    if (channels.empty()) {
        ImGui::TextDisabled("Shift+click a wire to add probe");
        ImGui::End();
        return;
    }

    float min_v = 0.0f;
    float max_v = 0.0f;
    visual::osc::compute_range(channels, min_v, max_v);

    const float avail_h = ImGui::GetContentRegionAvail().y;
    const int n = static_cast<int>(channels.size());
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float total_spacing = spacing * static_cast<float>(n > 0 ? (n - 1) : 0);
    float row_total_h = (n > 0) ? (avail_h - total_spacing) / static_cast<float>(n) : 80.0f;
    if (row_total_h < 80.0f) row_total_h = 80.0f;

    const float text_h = ImGui::GetTextLineHeight();
    const float inner_spacing = ImGui::GetStyle().ItemSpacing.y;
    float plot_h = row_total_h - text_h - text_h - inner_spacing * 2.0f;
    if (plot_h < 28.0f) plot_h = 28.0f;

    for (const auto& ch : channels) {
        if (!ch.probe) continue;
        // ImGui child ID: unique per wire within this window — uses PushID to avoid allocation.
        ImGui::PushID(static_cast<int>(ch.probe->wire_iid.raw()));
        ImGui::BeginChild("##osc", ImVec2(0.0f, row_total_h), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        visual::osc::render_channel_plot(*ch.probe, ch.probe->samples, min_v, max_v, plot_h);
        const std::deque<float> tail = visual::osc::visible_tail(ch.probe->samples);
        visual::osc::render_stats_row(OscilloscopeModel::compute_stats(tail, ws.oscilloscope.sample_period_sec()));
        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::End();
}
