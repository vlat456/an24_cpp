#include "color_picker_dialog.h"
#include "editor/window_system.h"
#include <imgui.h>

namespace an24 {

void ColorPickerDialog::render(WindowSystem& ws) {
    if (ws.colorPicker.show) {
        ImGui::OpenPopup("Node Color");
        ws.colorPicker.show = false;
    }
    
    if (ImGui::BeginPopupModal("Node Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Document* doc = ws.colorPicker.source_doc ? ws.colorPicker.source_doc : ws.activeDocument();
        if (doc && ws.colorPicker.node_index < doc->blueprint().nodes.size()) {
            BlueprintWindow* target_win = nullptr;
            if (!ws.colorPicker.group_id.empty()) {
                target_win = doc->windowManager().find(ws.colorPicker.group_id);
            }
            VisualScene* target_scene = target_win ? &target_win->scene : &doc->scene();
            
            Node& node = doc->blueprint().nodes[ws.colorPicker.node_index];
            if (node.color) {
                ws.colorPicker.rgba[0] = node.color->r;
                ws.colorPicker.rgba[1] = node.color->g;
                ws.colorPicker.rgba[2] = node.color->b;
                ws.colorPicker.rgba[3] = node.color->a;
            }
            
            ImGui::ColorPicker4("##picker", ws.colorPicker.rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);
            
            if (ImGui::Button("Apply")) {
                node.color = NodeColor{
                    ws.colorPicker.rgba[0],
                    ws.colorPicker.rgba[1],
                    ws.colorPicker.rgba[2],
                    ws.colorPicker.rgba[3]
                };
                auto* vn = target_scene->getVisualNode(ws.colorPicker.node_index);
                if (vn) vn->setCustomColor(node.color);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                node.color = std::nullopt;
                auto* vn = target_scene->getVisualNode(ws.colorPicker.node_index);
                if (vn) vn->setCustomColor(std::nullopt);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace an24
