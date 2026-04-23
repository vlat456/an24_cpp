#pragma once

#include "editor/window_system.h"
#include "editor/document.h"
#include "parse_number.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <string>

class InlineValueEditorDialog {
public:
    void render(WindowSystem& ws) {
#ifndef EDITOR_TESTING
        if (!ws.inlineValueEditor.open) {
            return;
        }

        Document* doc = ws.inlineValueEditor.document_id
            ? ws.findDocumentById(*ws.inlineValueEditor.document_id)
            : nullptr;
        if (!doc) {
            ws.inlineValueEditor.close();
            return;
        }

        const WindowScopeId& scope_id = ws.inlineValueEditor.scope_id;

        // Resolve the EditingHost: root scopes use the document's root host;
        // embedded scopes use the cached host created when the dialog opened.
        EditingHost* host = nullptr;
        if (scope_id.is_root()) {
            host = doc->root().host.get();
        } else if (scope_id.is_embedded()) {
            host = ws.inlineValueEditor.cached_host.get();
        } else {
            // External scopes are read-only — inline value editing is rejected.
            ws.inlineValueEditor.close();
            return;
        }

        if (!host) {
            ws.inlineValueEditor.close();
            return;
        }

        const ui::InternedId node_iid = ws.inlineValueEditor.node_id;
        if (node_iid.empty()) {
            ws.inlineValueEditor.close();
            return;
        }

        const bp2::Blueprint::Node* node = host->find_node(node_iid);
        if (!node) {
            ws.inlineValueEditor.close();
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowBgAlpha(0.95f);
        if (ws.inlineValueEditor.has_anchor) {
            ImGui::SetNextWindowPos(
                ImVec2(ws.inlineValueEditor.anchor_screen.x, ws.inlineValueEditor.anchor_screen.y),
                ImGuiCond_Appearing
            );
        }

        const std::string title = "Value##inline_" + std::to_string(ws.inlineValueEditor.node_id.raw());
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open,
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            const bool enter = ImGui::InputText(
                "##value_input",
                &ws.inlineValueEditor.buffer,
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll
            );

            bool commit = false;
            bool cancel = false;
            if (enter) {
                commit = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                cancel = true;
            }

            if (!ws.inlineValueEditor.error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ws.inlineValueEditor.error.c_str());
            }

            if (commit) {
                float parsed = 0.0f;
                if (ws.inlineValueEditor.buffer.empty() ||
                    !locale_safe::parse_float(ws.inlineValueEditor.buffer, parsed)) {
                    ws.inlineValueEditor.error = "Invalid number";
                } else {
                    const ui::InternedId value_key = doc->interner().intern("value");
                    host->update_node(node_iid, [&](bp2::Blueprint::Node& updated) {
                        updated.semantic.params[value_key] = parsed;
                    });
                    doc->rebuildAllWindows();
                    ws.inspector().markDirty();
                    ws.inlineValueEditor.close();
                }
            } else if (cancel || !open || (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
                ws.inlineValueEditor.close();
            }
        }
        ImGui::End();
#endif
    }
};
