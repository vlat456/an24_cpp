#pragma once

#include "editor/window_system.h"
#include <imgui.h>

namespace an24 {

/// Bake-in confirmation dialog for sub-blueprint embedding
class BakeInDialog {
public:
    void render(WindowSystem& ws) {
        if (ws.pendingBakeIn.show_confirmation) {
            ImGui::OpenPopup("Bake In Confirmation");
            ws.pendingBakeIn.show_confirmation = false;
        }
        
        if (ImGui::BeginPopupModal("Bake In Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to bake in this sub-blueprint?");
            ImGui::Text("This will embed all nodes from the library file directly into this document.");
            ImGui::Separator();
            
            if (ImGui::Button("Bake In")) {
                if (ws.pendingBakeIn.doc) {
                    ws.pendingBakeIn.doc->blueprint().bake_in_sub_blueprint(ws.pendingBakeIn.sub_blueprint_id);
                }
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
    }
};

} // namespace an24
