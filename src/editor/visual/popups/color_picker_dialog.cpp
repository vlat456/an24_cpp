#include "color_picker_dialog.h"
#include "editor/window_system.h"
#include "editor/document.h"
#include <imgui.h>

void ColorPickerDialog::render(WindowSystem& ws) {
    if (ws.colorPicker.show) {
        ImGui::OpenPopup("Node Color");
        ws.colorPicker.show = false;
    }

    if (ImGui::BeginPopupModal("Node Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Document* doc = ws.colorPicker.source_document_id
            ? ws.findDocumentById(*ws.colorPicker.source_document_id)
            : nullptr;
        if (!doc) {
            ws.colorPicker.source_document_id.reset();
            ws.colorPicker.show = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        core::InternedId const node_iid = ws.colorPicker.node_id;
        const bp2::Blueprint::Node* node_ptr = (doc && !node_iid.empty())
            ? doc->find_node_in_scope(ws.colorPicker.scope_id, ws.colorPicker.node_id)
            : nullptr;

        if (!node_ptr) {
            ws.colorPicker.source_document_id.reset();
            ws.colorPicker.show = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // NOTE: Do NOT overwrite rgba[] from node color here.
        // openColorPickerForNode() already initialised rgba[] once.
        // Overwriting every frame would fight the ImGui picker and
        // cause the selected value to snap back to the original color.

        ImGui::ColorPicker4("##picker", ws.colorPicker.rgba,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

        if (ImGui::Button("Apply")) {
            float const r = ws.colorPicker.rgba[0];
            float const g = ws.colorPicker.rgba[1];
            float const b = ws.colorPicker.rgba[2];
            float const a = ws.colorPicker.rgba[3];

            doc->set_node_color_for_scope(ws.colorPicker.scope_id, node_iid, editor::NodeColor{r, g, b, a});
            ws.colorPicker.source_document_id.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            doc->set_node_color_for_scope(ws.colorPicker.scope_id, node_iid, std::nullopt);
            ws.colorPicker.source_document_id.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ws.colorPicker.source_document_id.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
