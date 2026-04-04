#include "main_menu.h"
#include "editor/visual/dialogs/file_dialogs.h"
#include "editor/pi_zn_tuner.h"
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
    renderToolsMenu(ws);
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

    // Snapshot: openDocument() mutates recentFiles() via addRecentFile (erase+insert).
    const auto recent_snapshot = ws.settings.recentFiles();
    std::string deferred_open;

    for (size_t i = 0; i < recent_snapshot.size(); i++) {
        const std::string& recent_path = recent_snapshot[i];
        std::string name = std::filesystem::path(recent_path).filename().string();
        
        if (ImGui::MenuItem(name.c_str())) {
            deferred_open = recent_path;
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

    // Open after menu rendering is complete to avoid mutating during iteration
    if (!deferred_open.empty()) {
        ws.openDocument(deferred_open);
    }
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
    if (ImGui::MenuItem("Oscilloscope", nullptr, ws.showOscilloscope)) {
        ws.showOscilloscope = !ws.showOscilloscope;
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

void MainMenu::renderToolsMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Tools")) return;

    Document* active_doc = ws.activeDocument();
    auto run_zn = [&](bool apply_result) {
        if (!active_doc) return;
        ZNTuneConfig cfg;
        cfg.pi_node = ws.znTune.pi_node;
        cfg.feedback_signal = ws.znTune.feedback_signal;
        cfg.dt_sec = ws.znTune.cfg_dt_sec;
        cfg.run_time_sec = ws.znTune.cfg_run_time_sec;
        cfg.settle_time_sec = ws.znTune.cfg_settle_time_sec;
        cfg.kp_lo = ws.znTune.cfg_kp_lo;
        cfg.kp_hi = ws.znTune.cfg_kp_hi;
        cfg.max_expand = ws.znTune.cfg_max_expand;
        cfg.binary_iters = ws.znTune.cfg_binary_iters;
        cfg.min_peaks = ws.znTune.cfg_min_peaks;
        if (!active_doc->isSimulationRunning()) {
            active_doc->startSimulation();
        }

        ZNTuneResult r = tune_pi_ziegler_nichols(*active_doc, cfg, apply_result);
        ws.znTune.last_ok = r.ok;
        ws.znTune.last_was_preview = !apply_result;
        ws.znTune.last_cfg = cfg;
        ws.znTune.Ku = r.Ku;
        ws.znTune.Tu = r.Tu;
        ws.znTune.Kp = r.Kp;
        ws.znTune.Ki = r.Ki;
        std::memset(ws.znTune.error, 0, sizeof(ws.znTune.error));
        if (!r.ok) {
            std::strncpy(ws.znTune.error, r.error.c_str(), sizeof(ws.znTune.error) - 1);
        }
        ws.znTune.show_result_popup = true;
    };

    if (ImGui::MenuItem("Auto Tune PI (ZN)", nullptr, false, active_doc != nullptr)) {
        run_zn(true);
    }
    if (ImGui::MenuItem("Auto Tune PI (ZN) [Preview]", nullptr, false, active_doc != nullptr)) {
        run_zn(false);
    }

    ImGui::Separator();
    ImGui::InputText("PI node", ws.znTune.pi_node, sizeof(ws.znTune.pi_node));
    ImGui::InputText("Feedback", ws.znTune.feedback_signal, sizeof(ws.znTune.feedback_signal));
    ImGui::InputFloat("dt (s)", &ws.znTune.cfg_dt_sec, 0.0f, 0.0f, "%.5f");
    ImGui::InputFloat("run time (s)", &ws.znTune.cfg_run_time_sec, 0.0f, 0.0f, "%.2f");
    ImGui::InputFloat("settle (s)", &ws.znTune.cfg_settle_time_sec, 0.0f, 0.0f, "%.2f");
    ImGui::InputFloat("Kp low", &ws.znTune.cfg_kp_lo, 0.0f, 0.0f, "%.4f");
    ImGui::InputFloat("Kp high", &ws.znTune.cfg_kp_hi, 0.0f, 0.0f, "%.4f");
    ImGui::InputInt("max expand", &ws.znTune.cfg_max_expand);
    ImGui::InputInt("binary iters", &ws.znTune.cfg_binary_iters);
    ImGui::InputInt("min peaks", &ws.znTune.cfg_min_peaks);

    ws.znTune.cfg_dt_sec = std::max(1e-4f, ws.znTune.cfg_dt_sec);
    ws.znTune.cfg_run_time_sec = std::max(1.0f, ws.znTune.cfg_run_time_sec);
    ws.znTune.cfg_settle_time_sec = std::max(0.0f, ws.znTune.cfg_settle_time_sec);
    if (ws.znTune.cfg_settle_time_sec >= ws.znTune.cfg_run_time_sec) {
        ws.znTune.cfg_settle_time_sec = ws.znTune.cfg_run_time_sec * 0.5f;
    }
    ws.znTune.cfg_kp_lo = std::max(1e-6f, ws.znTune.cfg_kp_lo);
    ws.znTune.cfg_kp_hi = std::max(ws.znTune.cfg_kp_lo * 1.1f, ws.znTune.cfg_kp_hi);
    ws.znTune.cfg_max_expand = std::max(0, ws.znTune.cfg_max_expand);
    ws.znTune.cfg_binary_iters = std::max(1, ws.znTune.cfg_binary_iters);
    ws.znTune.cfg_min_peaks = std::max(2, ws.znTune.cfg_min_peaks);

    ImGui::EndMenu();
}
