#pragma once

#include "editor/window_system.h"
#include "editor/visual/dialogs/file_dialogs.h"
#include <imgui.h>
#include <cstring>


/// Modal dialog for setting the blueprint name (meta.name).
/// Triggered from the Blueprint menu ("Set Name...") or automatically
/// on first save when the name is empty.
class SetNameDialog {
public:
    void render(WindowSystem& ws) {
        if (ws.setName.show) {
            ImGui::OpenPopup("Set Blueprint Name");
            ws.setName.show = false;
        }

        if (ImGui::BeginPopupModal("Set Blueprint Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            Document* doc = ws.setName.document_id
                ? ws.findDocumentById(*ws.setName.document_id)
                : nullptr;
            if (!doc) {
                ws.setName.document_id.reset();
                ws.setName.show = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::Text("Enter a name for this blueprint:");
            ImGui::Separator();

            // Auto-focus the input field on first appearance
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            bool enter_pressed = ImGui::InputText(
                "##name", ws.setName.buf, sizeof(ws.setName.buf),
                ImGuiInputTextFlags_EnterReturnsTrue);

            bool name_valid = std::strlen(ws.setName.buf) > 0;

            if (!name_valid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Name cannot be empty.");
            }

            ImGui::Separator();

            if ((ImGui::Button("OK", ImVec2(120, 0)) || enter_pressed) && name_valid) {
                apply(ws);
                ws.setName.document_id.reset();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ws.setName.document_id.reset();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

private:
    void apply(WindowSystem& ws) {
        Document* doc = ws.setName.document_id
            ? ws.findDocumentById(*ws.setName.document_id)
            : nullptr;
        if (!doc) return;

        // Update the blueprint name via EditorModel (immutable update)
        doc->model().replace_current(
            doc->model().current()
                .with_name(ws.setName.buf));

        // If save was deferred until name was set, proceed with save now
        if (ws.setName.save_after) {
            ws.setName.save_after = false;
            if (doc->filepath().empty()) {
                if (auto path = dialogs::saveBlueprint()) {
                    doc->save(*path);
                    ws.settings.addRecentFile(*path);
                }
            } else {
                doc->save(doc->filepath());
            }
        }
    }
};
