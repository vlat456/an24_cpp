#include "editor_app.h"

#include "editor/gl_setup.h"
#include "editor/icon_font.h"
#include "editor/imgui_theme.h"
#include "editor/pi_zn_tuner.h"
#include "editor/visual/dialogs/file_dialogs.h"

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
#include <cstring>

namespace {

void save_active_document_with_existing_flow(WindowSystem& ws, Document* doc) {
    if (!doc) return;

    // Keep behavior aligned with File -> Save menu.
    if (doc->blueprint().name().empty()) {
        ws.setName.show = true;
        ws.setName.document_id = doc->id();
        ws.setName.save_after = true;
        std::memset(ws.setName.buf, 0, sizeof(ws.setName.buf));
    } else if (doc->filepath().empty()) {
        if (auto path = dialogs::saveBlueprint()) {
            doc->save(*path);
            ws.settings.addRecentFile(*path);
        }
    } else {
        doc->save(doc->filepath());
    }
}

} // namespace

static std::string getConfigPath() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    return appdata ? std::string(appdata) + "/an24/settings.cfg"
        : "C:/an24/settings.cfg";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/an24/settings.cfg"
        : "/tmp/an24/settings.cfg";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/an24/settings.cfg";
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/an24/settings.cfg"
        : "/tmp/an24/settings.cfg";
#endif
}

static void ensureConfigDir(const std::string& path) {
    std::error_code ec;
    auto dir = std::filesystem::path(path).parent_path();
    std::filesystem::create_directories(dir, ec);
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
    ImFont* fa_font = ImGuiTheme::LoadFontAwesome(io.Fonts, 14.0f);
    if (fa_font) {
        icon_font_.handle = fa_font;
    }
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
    
    std::string settings_path = getConfigPath();
    ensureConfigDir(settings_path);
    ws_.settings.loadFrom(settings_path);
    
    // Restore open tabs from previous session
    if (ws_.settings.hasOpenTabs()) {
        // IMPORTANT: snapshot the list before iterating.
        // openDocument() calls settings.addOpenTab() which mutates the same
        // vector returned by openTabs(), invalidating range-for iterators.
        const auto saved_tabs = ws_.settings.openTabs();  // copy
        bool had_failures = false;
        std::string failed_list;
        for (const auto& path : saved_tabs) {
            if (!ws_.openDocument(path)) {
                had_failures = true;
                if (!failed_list.empty()) failed_list += "\n";
                failed_list += path;
                // Remove from persisted tabs so we don't retry on every launch
                ws_.settings.removeOpenTab(path);
            }
        }
        if (had_failures) {
            pending_open_error_.show = true;
            pending_open_error_.message = "Failed to open one or more tabs:\n" + failed_list;
        }
        // Restore active tab focus
        if (!ws_.settings.activeTab().empty()) {
            if (Document* doc = ws_.findDocumentByPath(ws_.settings.activeTab())) {
                ws_.setActiveDocument(doc);
                ws_.setPendingTabFocus(doc);
            }
        }
    }
    
    running_ = true;
    while (running_) {
        handleEvents();
        update();
        render();
        
        // Track active tab for session restore
        if (Document* doc = ws_.activeDocument()) {
            if (!doc->filepath().empty()) {
                ws_.settings.setActiveTab(doc->filepath());
            } else {
                ws_.settings.setActiveTab("");
            }
        } else {
            ws_.settings.setActiveTab("");
        }
    }
    
    ws_.settings.saveTo(settings_path);
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
    
    // Save: Cmd+S (macOS) / Ctrl+S — always available, even when
    // properties panel is open or a text field has keyboard focus.
    if (Document* doc = ws_.activeDocument()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_S)) {
            save_active_document_with_existing_flow(ws_, doc);
        }
    }

    // Canvas-oriented shortcuts: blocked when a text field captures
    // the keyboard or the properties panel is open.
    if (!io.WantCaptureKeyboard && !ws_.propertiesWindow().is_open()) {
        if (Document* doc = ws_.activeDocument()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (doc->isSimulationRunning()) doc->stopSimulation();
                else doc->startSimulation();
            }
            // Undo: Cmd+Z (macOS) / Ctrl+Z (others)
            if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_Z)) {
                doc->performUndo();
            }
            // Redo: Cmd+Shift+Z (macOS) / Ctrl+Shift+Z, or Cmd+Y / Ctrl+Y
            if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z) ||
                ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_Y)) {
                doc->performRedo();
            }
        }
    }
    
    if (Document* doc = ws_.activeDocument()) {
        doc->updateSimulationStep(io.DeltaTime);
        doc->updateNodeContentFromSimulation();
        // NOTE: on_blueprint_changed re-resolves probe signal keys via string
        // construction. With 0-5 probes this is negligible. The main per-frame
        // allocation win comes from the NodeSignalCache (10-50 animated nodes,
        // now zero-allocation). Moving this to mutation sites only would require
        // Document → Oscilloscope coupling that doesn't currently exist.
        ws_.oscilloscope.on_blueprint_changed(*doc);
        ws_.oscilloscope.sample(*doc, doc->isSimulationRunning(), io.DeltaTime);
    }
}

void EditorApp::render() {
    auto& io = ImGui::GetIO();
    
    auto menu_result = main_menu_.render(ws_);
    if (menu_result.exit_requested) {
        running_ = false;
    }

    if (ws_.znTune.show_result_popup) {
        ImGui::OpenPopup("ZN PI Tune Result");
        ws_.znTune.show_result_popup = false;
    }

    if (pending_open_error_.show) {
        ImGui::OpenPopup("Open Tabs Error");
        pending_open_error_.show = false;
    }
    ImGui::SetNextWindowSize(ImVec2(680.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open Tabs Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", pending_open_error_.message.c_str());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            pending_open_error_.message.clear();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("ZN PI Tune Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ws_.znTune.last_ok) {
            ImGui::Text("Ku: %.6f", ws_.znTune.Ku);
            ImGui::Text("Tu: %.6f s", ws_.znTune.Tu);
            ImGui::Separator();
            ImGui::Text("PI tuned:");
            ImGui::Text("Kp: %.6f", ws_.znTune.Kp);
            ImGui::Text("Ki: %.6f", ws_.znTune.Ki);
        } else {
            ImGui::TextWrapped("ZN tune failed: %s", ws_.znTune.error);
            ImGui::Spacing();
            ImGui::TextDisabled("Try: increase run time, lower settle time, or start with higher Kp range.");
        }
        if (ws_.znTune.last_ok && ws_.znTune.last_was_preview) {
            ImGui::Separator();
            if (ImGui::Button("Apply")) {
                if (Document* doc = ws_.activeDocument()) {
                    std::string err;
                    bool ok = apply_pi_params(*doc, ws_.znTune.last_cfg.pi_node,
                                              ws_.znTune.Kp, ws_.znTune.Ki, &err);
                    if (!ok) {
                        std::memset(ws_.znTune.error, 0, sizeof(ws_.znTune.error));
                        std::strncpy(ws_.znTune.error, err.c_str(), sizeof(ws_.znTune.error) - 1);
                        ws_.znTune.last_ok = false;
                    } else {
                        ws_.znTune.last_was_preview = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply + Restart Sim")) {
                if (Document* doc = ws_.activeDocument()) {
                    std::string err;
                    bool ok = apply_pi_params(*doc, ws_.znTune.last_cfg.pi_node,
                                              ws_.znTune.Kp, ws_.znTune.Ki, &err);
                    if (!ok) {
                        std::memset(ws_.znTune.error, 0, sizeof(ws_.znTune.error));
                        std::strncpy(ws_.znTune.error, err.c_str(), sizeof(ws_.znTune.error) - 1);
                        ws_.znTune.last_ok = false;
                    } else {
                        doc->stopSimulation();
                        doc->startSimulation();
                        ws_.znTune.last_was_preview = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy values")) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "Ku=%.6f Tu=%.6f Kp=%.6f Ki=%.6f",
                              ws_.znTune.Ku, ws_.znTune.Tu, ws_.znTune.Kp, ws_.znTune.Ki);
                ImGui::SetClipboardText(buf);
            }
            ImGui::SameLine();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    
    float menu_height = ImGui::GetFrameHeight();
    float available_h = io.DisplaySize.y - menu_height;
    float available_w = io.DisplaySize.x;
    
    inspector_panel_.setVisible(ws_.showInspector);
    
    if (inspector_panel_.visible()) {
        auto inspector_result = inspector_panel_.render(ws_, menu_height, available_h, available_w);
        if (!inspector_result.selected_node_id.empty() && ws_.activeDocument()) {
            ws_.activeDocument()->input().select_node_by_id(inspector_result.selected_node_id);
        }
        ws_.showInspector = inspector_panel_.visible();
    }
    
    float canvas_x = inspector_panel_.totalWidth();

    ws_.reconcile_owner_bound_ui();
    
    auto doc_result = document_area_.render(ws_, canvas_x, menu_height, available_w - canvas_x, available_h);
    if (doc_result.close_requested) {
        ws_.closeDocument(*doc_result.close_requested);
    }
    
    sub_window_renderer_.renderAll(ws_);
    oscilloscope_window_.render(ws_);
    
    context_menus_.renderAddComponent(ws_);
    context_menus_.renderNodeContext(ws_);
    
    color_picker_.render(ws_);
    bake_in_dialog_.render(ws_);
    set_name_dialog_.render(ws_);
    extract_to_blueprint_dialog_.render(ws_);
    inline_value_editor_dialog_.render(ws_);

    ws_.propertiesWindow().render();
    
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.078f, 0.082f, 0.102f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    SDL_GL_SwapWindow(window_);
}
