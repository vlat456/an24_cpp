#pragma once

#include "editor/window_system.h"
#include "editor/visual/menu/main_menu.h"
#include "editor/visual/panels/inspector_panel.h"
#include "editor/visual/panels/document_area.h"
#include "editor/visual/windows/sub_window_renderer.h"
#include "editor/visual/popups/context_menus.h"
#include "editor/visual/popups/color_picker_dialog.h"
#include "editor/visual/popups/bake_in_dialog.h"
#include <memory>

struct SDL_Window;
typedef void* SDL_GLContext;

/// Editor application - encapsulates entire app lifecycle
class EditorApp {
public:
    EditorApp() = default;
    ~EditorApp();
    
    /// Run the editor - blocks until exit
    int run();
    
private:
    bool initSDL();
    bool initImGui();
    void shutdown();
    
    void handleEvents();
    void handleKeyboard();
    void update();
    void render();
    
    // Platform
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    bool running_ = false;
    bool shutdown_done_ = false;
    
    // Business logic
    ::WindowSystem ws_;
    
    // UI components (using global namespace classes)
    MainMenu main_menu_;
    InspectorPanel inspector_panel_;
    DocumentArea document_area_;
    SubWindowRenderer sub_window_renderer_;
    ContextMenus context_menus_;
    ColorPickerDialog color_picker_;
    BakeInDialog bake_in_dialog_;
};
