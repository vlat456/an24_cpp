#include "color_picker_dialog.h"
#include "editor/window_system.h"
#include <imgui.h>


/// Look up the visual widget for a node in the correct window's scene.
static visual::Widget* find_visual_widget(Document& doc,
                                          const std::string& node_id,
                                          const std::string& group_id) {
    BlueprintWindow* win = doc.windowManager().find(group_id);
    if (!win) return nullptr;
    return win->scene.find(node_id);
}

void ColorPickerDialog::render(WindowSystem& ws) {
    if (ws.colorPicker.show) {
        ImGui::OpenPopup("Node Color");
        ws.colorPicker.show = false;
    }
    
    if (ImGui::BeginPopupModal("Node Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Document* doc = ws.colorPicker.source_doc ? ws.colorPicker.source_doc : ws.activeDocument();
        Node* node_ptr = doc ? doc->blueprint().find_node(ws.colorPicker.node_id.c_str()) : nullptr;
        if (doc && node_ptr) {
            Node& node = *node_ptr;

            // NOTE: Do NOT overwrite rgba[] from node.color here.
            // openColorPickerForNode() already initialised rgba[] once.
            // Overwriting every frame would fight the ImGui picker and
            // cause the selected value to snap back to the original color.

            ImGui::ColorPicker4("##picker", ws.colorPicker.rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);
            
            if (ImGui::Button("Apply")) {
                NodeColor nc{
                    ws.colorPicker.rgba[0],
                    ws.colorPicker.rgba[1],
                    ws.colorPicker.rgba[2],
                    ws.colorPicker.rgba[3]
                };
                node.color = nc;

                // Update visual widget immediately so the colour change is
                // visible without requiring a blueprint reload.
                if (auto* w = find_visual_widget(*doc, ws.colorPicker.node_id,
                                                  ws.colorPicker.group_id)) {
                    w->setCustomColor(nc.to_uint32());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                node.color = std::nullopt;

                // Clear custom colour on the visual widget immediately.
                if (auto* w = find_visual_widget(*doc, ws.colorPicker.node_id,
                                                  ws.colorPicker.group_id)) {
                    w->setCustomColor(std::nullopt);
                }
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
