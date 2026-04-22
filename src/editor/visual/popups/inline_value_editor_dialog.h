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
            ws.inlineValueEditor.open = false;
            ws.inlineValueEditor.document_id.reset();
            return;
        }

        const WindowScopeId& scope_id = ws.inlineValueEditor.scope_id;

        // Resolve the EditingHost for the correct scope — this ensures
        // embedded nodes are found and mutated through the proper path.
        std::unique_ptr<EditingHost> owned_host;
        EditingHost* host = nullptr;
        if (scope_id.is_root()) {
            host = doc->root().host.get();
        } else if (scope_id.is_embedded()) {
            std::vector<ui::InternedId> path;
            path.reserve(scope_id.path().size());
            for (const std::string& segment : scope_id.path()) {
                const ui::InternedId iid = doc->interner().lookup(segment);
                if (iid.empty()) {
                    ws.inlineValueEditor.open = false;
                    ws.inlineValueEditor.document_id.reset();
                    return;
                }
                path.push_back(iid);
            }
            owned_host = create_pathful_embedded_host(doc->model(), std::move(path));
            host = owned_host.get();
        } else {
            // External scopes are read-only — inline value editing is rejected.
            ws.inlineValueEditor.open = false;
            ws.inlineValueEditor.document_id.reset();
            return;
        }

        if (!host) {
            ws.inlineValueEditor.open = false;
            ws.inlineValueEditor.document_id.reset();
            return;
        }

        const ui::InternedId node_iid = doc->interner().lookup(ws.inlineValueEditor.node_id.str());
        if (node_iid.empty()) {
            ws.inlineValueEditor.open = false;
            ws.inlineValueEditor.document_id.reset();
            return;
        }

        const bp2::Blueprint::Node* node = host->find_node(node_iid);
        if (!node) {
            ws.inlineValueEditor.open = false;
            ws.inlineValueEditor.document_id.reset();
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

        const std::string title = "Value##inline_" + ws.inlineValueEditor.node_id.str();
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
                    ws.inlineValueEditor.open = false;
                    ws.inlineValueEditor.document_id.reset();
                }
            } else if (cancel || !open || (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
                ws.inlineValueEditor.open = false;
                ws.inlineValueEditor.document_id.reset();
            }
        }
        ImGui::End();
#endif
    }
};
