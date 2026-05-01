#pragma once

#include "editor/document.h"
#include "editor/focus_scope.h"
#include "editor/window_system.h"

struct BlueprintWindow;

/// Main menu bar renderer — adapts content based on FocusScope.
///
/// Root focus:  File, Blueprint, Edit, View, Tools (full menu)
/// Subwindow:   Blueprint, Edit, View (no File/Tools)
/// Read-only:   Edit items disabled, Blueprint > Auto Layout disabled
class MainMenu {
public:
    struct Result {
        bool exit_requested = false;
    };

    Result render(WindowSystem& ws);

private:
    // Root-only menus.
    void renderFileMenu(WindowSystem& ws, Result& result);
    void renderToolsMenu(WindowSystem& ws);
    void renderAdaptersMenu(WindowSystem& ws);
    void renderRecentFilesMenu(WindowSystem& ws);
    void renderAboutDialog();

    // Scope-aware menus — operate on resolved FocusScope.
    void renderBlueprintMenu(WindowSystem& ws, const FocusScope::Resolved& focus);
    void renderEditMenu(WindowSystem& ws, const FocusScope::Resolved& focus);
    void renderViewMenu(WindowSystem& ws, const FocusScope::Resolved& focus);

    bool about_open_ = false;
};
