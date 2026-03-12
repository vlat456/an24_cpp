#include "splitter.h"
#include <imgui.h>
#include <algorithm>

namespace an24 {

uint64_t PanelSplitter::next_id_ = 0;

void PanelSplitter::render(float available_width, float available_height) {
    if (id_ == 0) {
        id_ = ++next_id_;
    }
    
    float max_size = (direction_ == SplitterDirection::Horizontal) 
        ? available_width * config_.max_size_ratio 
        : available_height * config_.max_size_ratio;
    
    float splitter_length = (direction_ == SplitterDirection::Horizontal)
        ? available_height
        : available_width;
    
    ImVec2 splitter_pos, splitter_size;
    if (direction_ == SplitterDirection::Horizontal) {
        splitter_pos = ImVec2(size_, 0);
        splitter_size = ImVec2(config_.thickness, splitter_length);
    } else {
        splitter_pos = ImVec2(0, size_);
        splitter_size = ImVec2(splitter_length, config_.thickness);
    }
    
    ImGui::SetNextWindowPos(splitter_pos);
    ImGui::SetNextWindowSize(splitter_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(config_.thickness, config_.thickness));
    
    char window_id[32];
    snprintf(window_id, sizeof(window_id), "##splitter_%llu", (unsigned long long)id_);
    
    ImGui::Begin(window_id, nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    
    char btn_id[32];
    snprintf(btn_id, sizeof(btn_id), "##btn_%llu", (unsigned long long)id_);
    ImGui::InvisibleButton(btn_id, splitter_size);
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(direction_ == SplitterDirection::Horizontal 
            ? ImGuiMouseCursor_ResizeEW 
            : ImGuiMouseCursor_ResizeNS);
    }
    
    if (ImGui::IsItemActive()) {
        float delta = direction_ == SplitterDirection::Horizontal 
            ? ImGui::GetIO().MouseDelta.x 
            : ImGui::GetIO().MouseDelta.y;
        size_ += delta;
        size_ = std::max(config_.min_size, std::min(size_, max_size));
    }
    
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace an24
