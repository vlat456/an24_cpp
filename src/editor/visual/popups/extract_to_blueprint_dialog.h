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

        if (ws.pendingExtract.has_preview) {
            const auto& p = ws.pendingExtract.preview;
            ImGui::Separator();
            ImGui::Text("Preview");
            ImGui::BulletText("Selected nodes: %zu", p.selected_nodes);
            ImGui::BulletText("Internal wires: %zu", p.internal_wires);
            ImGui::BulletText("Boundary inputs: %zu", p.input_count);
            ImGui::BulletText("Boundary outputs: %zu", p.output_count);

            if (!p.input_iface_names.empty()) {
                ImGui::TextUnformatted("Input ports:");
                for (const auto& name : p.input_iface_names) {
                    ImGui::BulletText("%s", name.c_str());
                }
            }
            if (!p.output_iface_names.empty()) {
                ImGui::TextUnformatted("Output ports:");
                for (const auto& name : p.output_iface_names) {
                    ImGui::BulletText("%s", name.c_str());
                }
            }
            if (!p.iface_collision_names.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   "Cannot extract: input/output iface name collision");
                for (const auto& name : p.iface_collision_names) {
                    ImGui::BulletText("%s", name.c_str());
                }
            }
        } else if (!ws.pendingExtract.preview_error.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "Preview failed: %s", ws.pendingExtract.preview_error.c_str());
        }

        const bool can_extract = ws.pendingExtract.has_preview
            && ws.pendingExtract.preview.iface_collision_names.empty();

        if (!can_extract) {
            ImGui::BeginDisabled();
        }
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
            ws.pendingExtract.has_preview = false;
            ws.pendingExtract.preview = {};
            ws.pendingExtract.preview_error.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!can_extract) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ws.pendingExtract.doc_id.clear();
            ws.pendingExtract.group_id.clear();
            ws.pendingExtract.selected_node_ids.clear();
            ws.pendingExtract.has_preview = false;
            ws.pendingExtract.preview = {};
            ws.pendingExtract.preview_error.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
};
