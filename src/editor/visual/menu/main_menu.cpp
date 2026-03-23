#include "main_menu.h"
#include "editor/visual/dialogs/file_dialogs.h"
#include <imgui.h>
#include <filesystem>
#include <cstring>


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
    renderBlueprintMenu(ws);
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
            // If blueprint has no name yet, prompt for one before saving
            if (active_doc->blueprint().name().empty()) {
                ws.setName.show = true;
                ws.setName.doc_id = active_doc->id();
                ws.setName.save_after = true;
                std::memset(ws.setName.buf, 0, sizeof(ws.setName.buf));
            } else if (active_doc->filepath().empty()) {
                if (auto path = dialogs::saveBlueprint()) {
                    active_doc->save(*path);
                    ws.settings.addRecentFile(*path);
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
    if (!ImGui::BeginMenu("Recent Files", !ws.settings.recentFiles().empty())) return;

    for (size_t i = 0; i < ws.settings.recentFiles().size(); i++) {
        const std::string& recent_path = ws.settings.recentFiles()[i];
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
        ws.settings.clearRecentFiles();
    }

    ImGui::EndMenu();
}

void MainMenu::renderEditMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Edit")) return;

    Document* active_doc = ws.activeDocument();
    bool props_open = ws.propertiesWindow().is_open();
    bool can_undo = active_doc && active_doc->canUndo() && !props_open;
    bool can_redo = active_doc && active_doc->canRedo() && !props_open;
    bool has_sel = active_doc && !active_doc->input().selected_nodes().empty();

    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_undo)) {
        if (active_doc) active_doc->performUndo();
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, can_redo)) {
        if (active_doc) active_doc->performRedo();
    }

    ImGui::Separator();

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
            active_doc->viewport().zoom *= 1.1f;
            active_doc->viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            active_doc->viewport().zoom /= 1.1f;
            active_doc->viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
            active_doc->viewport().zoom = 1.0f;
        }
    }

    ImGui::EndMenu();
}

void MainMenu::renderBlueprintMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Blueprint")) return;

    Document* active_doc = ws.activeDocument();

    // Show current name (or "not set")
    if (active_doc && !active_doc->blueprint().name().empty()) {
        ImGui::TextDisabled("Name: %s", active_doc->blueprint().name().c_str());
    } else if (active_doc && !active_doc->blueprint().display_name().empty()) {
        ImGui::TextDisabled("Name: %s", active_doc->blueprint().display_name().c_str());
    } else {
        ImGui::TextDisabled("Name: (not set)");
    }
    ImGui::Separator();

    if (ImGui::MenuItem("Set Name...", nullptr, false, active_doc != nullptr)) {
        if (active_doc) {
            ws.setName.show = true;
            ws.setName.doc_id = active_doc->id();
            ws.setName.save_after = false;
            std::memset(ws.setName.buf, 0, sizeof(ws.setName.buf));
            // Pre-fill with current name
            const auto& current = active_doc->blueprint().name();
            const auto& fallback = active_doc->blueprint().display_name();
            if (!current.empty()) {
                std::strncpy(ws.setName.buf, current.c_str(),
                             sizeof(ws.setName.buf) - 1);
            } else if (!fallback.empty()) {
                std::strncpy(ws.setName.buf, fallback.c_str(),
                             sizeof(ws.setName.buf) - 1);
            }
        }
    }

    ImGui::EndMenu();
}
