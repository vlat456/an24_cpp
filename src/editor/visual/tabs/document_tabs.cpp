#include "document_tabs.h"
#include "editor/window_system.h"
#include <imgui.h>


DocumentTabs::Result DocumentTabs::render(WindowSystem& ws) {
    Result result;

    if (!ImGui::BeginTabBar("DocumentTabs")) {
        return result;
    }

    tab_bar_height_ = ImGui::GetItemRectSize().y;

    for (const auto& doc : ws.documents()) {
        bool tab_open = true;
        std::string tab_label = doc->title() + "###" + doc->id();

        if (ImGui::BeginTabItem(tab_label.c_str(), &tab_open, ImGuiTabItemFlags_None)) {
            if (ws.activeDocument() != doc.get()) {
                ws.setActiveDocument(doc.get());
            }
            ImGui::EndTabItem();
        }

        if (!tab_open) {
            result.close_requested = doc.get();
        }
    }

    ImGui::EndTabBar();
    return result;
}

