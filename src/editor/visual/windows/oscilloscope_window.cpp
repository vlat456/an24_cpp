#include "oscilloscope_window.h"

#include "editor/document.h"

#include <imgui.h>
#include <algorithm>

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
    bool has_val = false;
    for (const auto& ch : channels) {
        if (!ch.probe || !ch.samples || ch.samples->empty()) continue;
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
        min_v = -1.0f;
        max_v = 1.0f;
    } else if (max_v - min_v < 1e-3f) {
        min_v -= 1.0f;
        max_v += 1.0f;
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const int channels_count = static_cast<int>(channels.size());
    const float total_spacing = spacing * static_cast<float>(std::max(0, channels_count - 1));
    float row_h = (channels_count > 0)
        ? (avail_h - total_spacing) / static_cast<float>(channels_count)
        : 56.0f;
    if (row_h < 40.0f) row_h = 40.0f;

    for (const auto& ch : channels) {
        if (!ch.probe || !ch.samples) continue;
        const std::string title = ch.probe->label + "###" + ch.probe->wire_id;

        ImGui::PushStyleColor(ImGuiCol_PlotLines, ch.probe->color);
        if (ch.samples->empty()) {
            float zero = 0.0f;
            ImGui::PlotLines(title.c_str(), &zero, 1, 0, "", min_v, max_v, ImVec2(-1.0f, row_h));
        } else {
            std::vector<float> vals(ch.samples->begin(), ch.samples->end());
            ImGui::PlotLines(title.c_str(), vals.data(), static_cast<int>(vals.size()), 0, "", min_v, max_v, ImVec2(-1.0f, row_h));
        }
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
