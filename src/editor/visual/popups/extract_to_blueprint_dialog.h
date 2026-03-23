#pragma once

#include "editor/window_system.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

class ExtractToBlueprintDialog {
public:
    void render(WindowSystem& ws) {
        if (ws.pendingExtract.show_dialog) {
            ImGui::OpenPopup("Extract to Blueprint");
            ws.pendingExtract.show_dialog = false;
        }

        if (!ImGui::BeginPopupModal("Extract to Blueprint", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("Extract selected nodes into embedded blueprint");
        ImGui::Separator();
        ImGui::Text("Blueprint name:");
        ImGui::InputText("##extract_name", ws.pendingExtract.name_buf, sizeof(ws.pendingExtract.name_buf));

        if (ImGui::Button("Extract")) {
            Document* doc = ws.findDocumentById(ws.pendingExtract.doc_id);
            if (!doc) doc = ws.activeDocument();
            if (doc) {
                std::string err;
                const bool ok = doc->extractToBlueprint(
                    ws.pendingExtract.selected_node_ids,
                    std::string(ws.pendingExtract.name_buf),
                    ws.pendingExtract.group_id,
                    &err);
                if (!ok) {
                    spdlog::warn("[extract] failed: {}", err);
                }
            }
            ws.pendingExtract.doc_id.clear();
            ws.pendingExtract.group_id.clear();
            ws.pendingExtract.selected_node_ids.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ws.pendingExtract.doc_id.clear();
            ws.pendingExtract.group_id.clear();
            ws.pendingExtract.selected_node_ids.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
};
