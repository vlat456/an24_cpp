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
        const bool name_changed = ImGui::InputText("##extract_name", ws.pendingExtract.name_buf, sizeof(ws.pendingExtract.name_buf));
        const bool mode_changed = ImGui::Checkbox(
            "Allow non-embedded descendant references (advanced)",
            &ws.pendingExtract.allow_nonembedded_descendant_refs);

        Document* doc_for_preview = ws.pendingExtract.document_id
            ? ws.findDocumentById(*ws.pendingExtract.document_id)
            : nullptr;
        if (!doc_for_preview) {
            ws.pendingExtract.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const std::string current_name(ws.pendingExtract.name_buf);
        const bool needs_initial_preview = !ws.pendingExtract.has_preview
            && ws.pendingExtract.preview_error.empty();
        const bool preview_stale = name_changed || mode_changed || needs_initial_preview;
        if (preview_stale && doc_for_preview) {
            ws.pendingExtract.preview = {};
            ws.pendingExtract.preview_error.clear();
            auto preview = editor::commands::build_extract_to_blueprint_preview(
                doc_for_preview->blueprint(),
                ws.pendingExtract.selected_node_ids,
                current_name,
                ws.pendingExtract.scope_id,
                doc_for_preview->interner(),
                doc_for_preview->arena(),
                ws.typeRegistry(),
                &ws.pendingExtract.preview_error,
                ws.pendingExtract.allow_nonembedded_descendant_refs);
            if (preview) {
                ws.pendingExtract.preview = *preview;
                ws.pendingExtract.has_preview = true;
                ws.pendingExtract.preview_name = current_name;
                ws.pendingExtract.preview_allow_nonembedded_descendant_refs =
                    ws.pendingExtract.allow_nonembedded_descendant_refs;
            } else {
                ws.pendingExtract.has_preview = false;
                ws.pendingExtract.preview_name = current_name;
                ws.pendingExtract.preview_allow_nonembedded_descendant_refs =
                    ws.pendingExtract.allow_nonembedded_descendant_refs;
            }
        }

        if (ws.pendingExtract.has_preview) {
            const auto& p = ws.pendingExtract.preview;
            ImGui::Separator();
            ImGui::Text("Preview");
            ImGui::BulletText("Selected nodes: %zu", p.selected_nodes);
            ImGui::BulletText("Internal wires: %zu", p.internal_wires);
            ImGui::BulletText("Boundary inputs: %zu", p.input_count);
            ImGui::BulletText("Boundary outputs: %zu", p.output_count);
            if (ws.pendingExtract.allow_nonembedded_descendant_refs) {
                ImGui::BulletText("Descendant refs remapped: %zu", p.remapped_descendant_refs);
                ImGui::BulletText("Descendant refs passthrough: %zu", p.passthrough_descendant_refs);
                ImGui::TextUnformatted("Remap tie-break: lowest provider node ID for matching blueprint_id.");
            }

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
            Document* doc = ws.pendingExtract.document_id
                ? ws.findDocumentById(*ws.pendingExtract.document_id)
                : nullptr;
            if (doc) {
                std::string err;
                const bool ok = doc->extractToBlueprint(
                    ws.pendingExtract.selected_node_ids,
                    std::string(ws.pendingExtract.name_buf),
                    ws.pendingExtract.scope_id,
                    &err,
                    ws.pendingExtract.allow_nonembedded_descendant_refs);
                if (!ok) {
                    spdlog::warn("[extract] failed: {}", err);
                }
            }
            ws.pendingExtract.reset();
            ImGui::CloseCurrentPopup();
        }
        if (!can_extract) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ws.pendingExtract.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
};
