#include "main_menu.h"
#include "editor/visual/dialogs/file_dialogs.h"
#include "editor/window/blueprint_window.h"
#include "editor/pi_zn_tuner.h"
#include "core/solvers/jit/bridge/simvar_provider_host.h"
#include <build_info.h>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <cstring>


MainMenu::Result MainMenu::render(WindowSystem& ws) {
    Result result;

    if (!ImGui::BeginMainMenuBar()) {
        return result;
    }

    // Resolve IDs to live pointers (safe — returns nulls if closed).
    const auto focus = ws.resolve_focus();

    // Simulation indicator — always visible when running.
    if (focus.document && focus.document->isSimulationRunning()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ">> SIM");
    }

    // Root-only menus.
    if (focus.is_root()) {
        renderFileMenu(ws, result);
    }

    // Scope-aware menus — require valid focus.
    if (focus.is_valid()) {
        renderBlueprintMenu(ws, focus);
        renderEditMenu(ws, focus);
        renderViewMenu(ws, focus);
    }

    // Root-only menus.
    if (focus.is_root()) {
        renderToolsMenu(ws);
    }

    // Adapters menu — always visible when a document is open.
    // Connection is a persistent user preference, independent of sim state.
    if (ws.activeDocument()) {
        renderAdaptersMenu(ws);
    }

#ifndef NDEBUG
    // Build number — last menu item, DEBUG only, no handler.
    ImGui::MenuItem(BUILD_NUMBER, nullptr, false, false);
#endif

    // About dialog (opened from File menu).
    renderAboutDialog();

    ImGui::EndMainMenuBar();
    return result;
}

// =============================================================================
// Root-only menus
// =============================================================================

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
            if (active_doc->blueprint().name().empty()) {
                ws.setName.show = true;
                ws.setName.document_id = active_doc->id();
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

    ImGui::Separator();

    if (ImGui::MenuItem("About")) {
        about_open_ = true;
    }

    ImGui::EndMenu();
}

void MainMenu::renderRecentFilesMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Recent Files", !ws.settings.recentFiles().empty())) return;

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

    if (!deferred_open.empty()) {
        ws.openDocument(deferred_open);
    }
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

void MainMenu::renderAdaptersMenu(WindowSystem& ws) {
    if (!ImGui::BeginMenu("Adapters")) return;

    Document* active_doc = ws.activeDocument();
    if (!active_doc) {
        ImGui::TextDisabled("No document open");
        ImGui::EndMenu();
        return;
    }

    SimvarProviderHost* host = active_doc->provider_host();
    if (!host) {
        ImGui::TextDisabled("Provider host unavailable");
        ImGui::EndMenu();
        return;
    }

    auto types = SimvarProviderHost::registered_types();
    if (types.empty()) {
        ImGui::MenuItem("No adapters installed", nullptr, false, false);
        ImGui::EndMenu();
        return;
    }

    for (const auto& type : types) {
        bool enabled = host->is_enabled(type);
        std::string label = type + (enabled ? "   [On]" : "   [Off]");
        if (ImGui::MenuItem(label.c_str())) {
            host->toggle_enabled(type);
        }
    }

    ImGui::EndMenu();
}

// =============================================================================
// Scope-aware menus
// =============================================================================

void MainMenu::renderBlueprintMenu(WindowSystem& ws, const FocusScope::Resolved& focus) {
    if (!ImGui::BeginMenu("Blueprint")) return;

    Document* doc = focus.document;
    BlueprintWindow* win = focus.window;

    // Show context: subwindow scope name or root blueprint name.
    if (focus.is_subwindow()) {
        ImGui::TextDisabled("Scope: %s", win->title.c_str());
        if (win->read_only) {
            ImGui::SameLine();
            ImGui::TextDisabled("[Read Only]");
        }
    } else {
        if (!doc->blueprint().name().empty()) {
            ImGui::TextDisabled("Name: %s", doc->blueprint().name().c_str());
        } else {
            ImGui::TextDisabled("Name: (not set)");
        }
    }
    ImGui::Separator();

    // Fit View — scope-aware viewport.
    if (ImGui::MenuItem("Fit View", nullptr, false, focus.is_valid())) {
        win->pending_auto_fit = true;
    }

    // Auto Layout — scope-aware.
    if (ImGui::MenuItem("Auto Layout", nullptr, false, !focus.is_read_only())) {
        win->input.cancel_gesture();
        if (focus.is_root()) {
            doc->autoLayout();
        } else {
            doc->autoLayoutEmbedded(win->resolved_scope_id());
        }
        win->pending_auto_fit = true;
    }

    // Set Name — root only.
    if (focus.is_root()) {
        ImGui::Separator();
        if (ImGui::MenuItem("Set Name...", nullptr, false, doc != nullptr)) {
            ws.setName.show = true;
            ws.setName.document_id = doc->id();
            ws.setName.save_after = false;
            std::memset(ws.setName.buf, 0, sizeof(ws.setName.buf));
            const auto& current = doc->blueprint().name();
            if (!current.empty()) {
                std::strncpy(ws.setName.buf, current.c_str(),
                             sizeof(ws.setName.buf) - 1);
            }
        }
    }

    ImGui::EndMenu();
}

void MainMenu::renderEditMenu(WindowSystem& ws, const FocusScope::Resolved& focus) {
    if (!ImGui::BeginMenu("Edit")) return;

    Document* doc = focus.document;
    BlueprintWindow* win = focus.window;

    bool props_open = ws.propertiesWindow().is_open();
    bool can_undo = doc->canUndo() && !props_open;
    bool can_redo = doc->canRedo() && !props_open;
    bool has_sel = win && !win->input.selected_node_ids().empty();
    bool writable = !focus.is_read_only();

    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_undo && writable)) {
        doc->performUndo();
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, can_redo && writable)) {
        doc->performRedo();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete", "Del", false, has_sel && writable)) {
        auto action = doc->applyInputResult(
            win->input.on_key(Key::Delete), win->resolved_scope_id());
        ws.handleInputAction(action, *doc);
    }

    ImGui::EndMenu();
}

void MainMenu::renderViewMenu(WindowSystem& ws, const FocusScope::Resolved& focus) {
    if (!ImGui::BeginMenu("View")) return;

    // Global toggles — always visible.
    if (ImGui::MenuItem("Inspector", nullptr, ws.showInspector)) {
        ws.showInspector = !ws.showInspector;
    }
    if (ImGui::MenuItem("Oscilloscope", nullptr, ws.showOscilloscope)) {
        ws.showOscilloscope = !ws.showOscilloscope;
    }
    if (ImGui::MenuItem("Debug Layout Bounds", nullptr, ws.showDebugLayoutBounds)) {
        ws.showDebugLayoutBounds = !ws.showDebugLayoutBounds;
    }
    if (ImGui::MenuItem("Debug Paint Bounds", nullptr, ws.showDebugPaintBounds)) {
        ws.showDebugPaintBounds = !ws.showDebugPaintBounds;
    }

    // Scope-dependent operations.
    if (focus.is_valid()) {
        BlueprintWindow* win = focus.window;
        Document* doc = focus.document;
        bool writable = !focus.is_read_only();

        ImGui::Separator();
        if (ImGui::MenuItem("Shrink Nodes To Fit", nullptr, false, writable)) {
            doc->normalizeNodeSizesToFit(false);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
            win->viewport.zoom *= 1.1f;
            win->viewport.clamp_zoom();
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            win->viewport.zoom /= 1.1f;
            win->viewport.clamp_zoom();
        }
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
            win->viewport.zoom = 1.0f;
        }
    }

    ImGui::EndMenu();
}

// =============================================================================
// About dialog
// =============================================================================

void MainMenu::renderAboutDialog() {
    if (!about_open_) return;

    ImGui::OpenPopup("About");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About", &about_open_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("An-24 Flight Simulator");
        ImGui::Spacing();
        ImGui::TextDisabled("Build %s", BUILD_NUMBER);
        ImGui::TextDisabled("%s", BUILD_DATE);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            about_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
