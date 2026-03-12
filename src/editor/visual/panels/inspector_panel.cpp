#include "inspector_panel.h"
#include "editor/window_system.h"
#include <imgui.h>


InspectorPanel::InspectorPanel() 
    : splitter_(std::make_unique<PanelSplitter>(SplitterDirection::Horizontal, width_)) {
}

InspectorPanel::Result InspectorPanel::render(::WindowSystem& ws, float menu_height, 
                                               float available_height, float available_width) {
    Result result;
    
    if (!visible_) return result;
    
    // Inspector window
    ImGui::SetNextWindowPos(ImVec2(0, menu_height));
    ImGui::SetNextWindowSize(ImVec2(width_, available_height));
    ImGui::Begin("Inspector", &visible_,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse);
    
    ws.inspector().render();
    result.selected_node_id = ws.inspector().consumeSelection();
    
    ImGui::End();
    
    // Splitter
    splitter_->render(available_width, available_height);
    
    return result;
}
