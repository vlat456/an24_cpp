#include "document_tabs.h"
#include "editor/window_system.h"
#include "editor/document.h"
#include <imgui.h>

DocumentTabs::Result DocumentTabs::render(::WindowSystem& ws) {
    Result result;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    if (!ImGui::BeginTabBar("##DocumentTabs", ImGuiTabBarFlags_None)) {
        ImGui::PopStyleVar();
        return result;
    }
    ImGui::PopStyleVar();

    tab_bar_height_ = ImGui::GetItemRectSize().y;

    // One-shot: grab and consume the pending focus so SetSelected applies for exactly one frame
    Document* focus_target = ws.pendingTabFocus();

    for (const auto& doc : ws.documents()) {
        // ImGui uses tab labels as IDs. Different documents can share the same
        // visible title (e.g. multiple untitled docs), so attach a stable
        // invisible suffix to keep tab IDs unique.
        std::string tab_label = doc->title();
        tab_label += "###doc_tab_";
        tab_label += doc->id().str();

        bool tab_open = true;
        ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
        if (focus_target == doc.get()) {
            flags |= ImGuiTabItemFlags_SetSelected;
        }

        if (ImGui::BeginTabItem(tab_label.c_str(), &tab_open, flags)) {
            if (ws.activeDocument() != doc.get()) {
                ws.setActiveDocument(doc.get());
            }
            ImGui::EndTabItem();
        }

        if (!tab_open) {
            result.close_requested = doc.get();
        }
    }

    // Clear after the full tab bar is rendered so the flag was applied exactly once
    if (focus_target) {
        ws.consumeTabFocus();
    }

    ImGui::EndTabBar();
    return result;
}
