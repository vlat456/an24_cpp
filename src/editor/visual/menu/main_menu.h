#pragma once

#include "editor/document.h"
#include "editor/window_system.h"


/// Main menu bar renderer
class MainMenu {
public:
    struct Result {
        bool exit_requested = false;
    };

    Result render(WindowSystem& ws);

private:
    void renderFileMenu(WindowSystem& ws, Result& result);
    void renderBlueprintMenu(WindowSystem& ws);
    void renderViewMenu(WindowSystem& ws);
    void renderEditMenu(WindowSystem& ws);
    void renderRecentFilesMenu(WindowSystem& ws);
};

