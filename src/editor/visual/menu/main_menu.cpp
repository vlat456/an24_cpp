#include "main_menu.h"
#include "editor/visual/dialogs/file_dialogs.h"
#include <imgui.h>
#include <filesystem>


MainMenu::Result MainMenu::render(WindowSystem& ws) {
    Result result;
    
    if (!ImGui::BeginMainMenuBar()) {
        return result;
    }

    Document* active_doc = ws.activeDocument();

    // Simulation indicator
    if (active_doc && active_doc->isSimulationRunning()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ">> SIM");
    }

    renderFileMenu(ws, result);
    renderEditMenu(ws);
    renderViewMenu(ws);

    ImGui::EndMainMenuBar();
    return result;
}

void MainMenu::renderFileMenu(WindowSystem& ws, Result& result) {
    if (!ImGui::BeginMenu("File")) return;

    Document* active_doc = ws.activeDocument();

    if (ImGui::MenuItem("New", "Ctrl+N")) {
        ws.createDocument();
    }

    if (ImGui::MenuItem("Open...", "Ctrl+O")) {
        if (auto path = dialogs::openBlueprint()) {
            ws.openDocument(*path);
        }
    }

    renderRecentFilesMenu(ws);

    if (ImGui::MenuItem("Save", "Ctrl+S", false, active_doc != nullptr)) {
        if (active_doc) {
            if (active_doc->filepath().empty()) {
                if (auto path = dialogs::saveBlueprint()) {
                    active_doc->save(*path);
                    ws.recent_files.add(*path);
                }
            } else {
                active_doc->save(active_doc->filepath());
            }
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Close Tab", nullptr, false, ws.documentCount() > 1)) {
        if (active_doc) ws.closeDocument(*active_doc);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Exit", "Alt+F4")) {
        result.exit_requested = true;
    }

    ImGui::EndMenu();
}

void MainMenu::renderRecentFilesMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Recent Files", !ws.recent_files.empty())) return;

    for (size_t i = 0; i < ws.recent_files.files().size(); i++) {
        const std::string& recent_path = ws.recent_files.files()[i];
        std::string name = std::filesystem::path(recent_path).filename().string();
        
        if (ImGui::MenuItem(name.c_str())) {
            std::string path_copy = recent_path;
            ws.openDocument(path_copy);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", recent_path.c_str());
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Clear List")) {
        ws.recent_files.clear();
    }

    ImGui::EndMenu();
}

void MainMenu::renderEditMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Edit")) return;

    Document* active_doc = ws.activeDocument();
    bool has_sel = active_doc && !active_doc->input().selected_nodes().empty();

    if (ImGui::MenuItem("Delete", "Del", false, has_sel)) {
        if (active_doc) {
            auto action = active_doc->applyInputResult(active_doc->input().on_key(Key::Delete));
            ws.handleInputAction(action, *active_doc);
        }
    }

    ImGui::EndMenu();
}

void MainMenu::renderViewMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("View")) return;

    if (ImGui::MenuItem("Inspector", nullptr, ws.showInspector)) {
        ws.showInspector = !ws.showInspector;
    }

    Document* active_doc = ws.activeDocument();
    if (active_doc) {
        ImGui::Separator();
        if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
            active_doc->scene().viewport().zoom *= 1.1f;
            active_doc->scene().viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            active_doc->scene().viewport().zoom /= 1.1f;
            active_doc->scene().viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
            active_doc->scene().viewport().zoom = 1.0f;
        }
    }

    ImGui::EndMenu();
}

