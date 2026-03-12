#include "editor_app.h"

#include "editor/gl_setup.h"
#include "editor/imgui_theme.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL2/SDL.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <cstdio>
#include <filesystem>

static std::string getConfigPath() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    return appdata ? std::string(appdata) + "/an24/recent_files.cfg"
        : "C:/an24/recent_files.cfg";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/an24/recent_files.cfg"
        : "/tmp/an24/recent_files.cfg";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/an24/recent_files.cfg";
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/an24/recent_files.cfg"
        : "/tmp/an24/recent_files.cfg";
#endif
}

static void ensureConfigDir(const std::string& path) {
    auto dir = std::filesystem::path(path).parent_path();
    std::filesystem::create_directories(dir);
}



EditorApp::~EditorApp() {
    shutdown();
}

bool EditorApp::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    
    const char* glsl_version = gl_setup::GLSL_VERSION;
    
    if (gl_setup::FORWARD_COMPAT) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    }
    if (gl_setup::CORE_PROFILE) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    }
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_setup::GL_MAJOR);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_setup::GL_MINOR);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, gl_setup::DOUBLE_BUFFER);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, gl_setup::DEPTH_SIZE);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, gl_setup::STENCIL_SIZE);
    
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    
    window_ = SDL_CreateWindow("AN-24 Blueprint Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900, window_flags);
    if (!window_) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    
    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        printf("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(1);
    
    printf("OpenGL: %s\n", glGetString(GL_VERSION));
    printf("GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    return true;
}

bool EditorApp::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    
    ImGuiTheme::LoadRobotoWithCyrillic(18.0f);
    ImGuiTheme::ApplyModernDarkTheme();
    
    ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
    ImGui_ImplOpenGL3_Init(gl_setup::GLSL_VERSION);
    
    return true;
}

void EditorApp::shutdown() {
    if (shutdown_done_) return;
    shutdown_done_ = true;
    
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    
    if (gl_context_) SDL_GL_DeleteContext(gl_context_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

int EditorApp::run() {
    if (!initSDL()) return -1;
    if (!initImGui()) {
        shutdown();
        return -1;
    }
    
    std::string recent_cfg = getConfigPath();
    ensureConfigDir(recent_cfg);
    ws_.recent_files.loadFrom(recent_cfg);
    
    running_ = true;
    while (running_) {
        handleEvents();
        update();
        render();
    }
    
    ws_.recent_files.saveTo(recent_cfg);
    shutdown();
    return 0;
}

void EditorApp::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        
        if (event.type == SDL_QUIT) {
            running_ = false;
        }
    }
}

void EditorApp::update() {
    auto& io = ImGui::GetIO();
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    
    if (!io.WantCaptureKeyboard) {
        if (Document* doc = ws_.activeDocument()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (doc->isSimulationRunning()) doc->stopSimulation();
                else doc->startSimulation();
            }
        }
    }
    
    if (Document* doc = ws_.activeDocument()) {
        doc->updateSimulationStep(io.DeltaTime);
        doc->updateNodeContentFromSimulation();
    }
}

void EditorApp::render() {
    auto& io = ImGui::GetIO();
    
    auto menu_result = main_menu_.render(ws_);
    if (menu_result.exit_requested) {
        running_ = false;
    }
    
    float menu_height = ImGui::GetFrameHeight();
    float available_h = io.DisplaySize.y - menu_height;
    float available_w = io.DisplaySize.x;
    
    inspector_panel_.setVisible(ws_.showInspector);
    
    if (inspector_panel_.visible()) {
        auto inspector_result = inspector_panel_.render(ws_, menu_height, available_h, available_w);
        if (!inspector_result.selected_node_id.empty() && ws_.activeDocument()) {
            ws_.activeDocument()->input().selectNodeById(inspector_result.selected_node_id);
        }
        ws_.showInspector = inspector_panel_.visible();
    }
    
    float canvas_x = inspector_panel_.totalWidth();
    
    auto doc_result = document_area_.render(ws_, canvas_x, menu_height, available_w - canvas_x, available_h);
    if (doc_result.close_requested) {
        ws_.closeDocument(*doc_result.close_requested);
    }
    
    sub_window_renderer_.renderAll(ws_);
    
    context_menus_.renderAddComponent(ws_);
    context_menus_.renderNodeContext(ws_);
    
    color_picker_.render(ws_);
    bake_in_dialog_.render(ws_);
    
    ws_.propertiesWindow().render();
    
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.078f, 0.082f, 0.102f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    SDL_GL_SwapWindow(window_);
}
