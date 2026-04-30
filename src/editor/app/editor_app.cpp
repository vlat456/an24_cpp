#include "editor_app.h"

#include "editor/gl_setup.h"
#include "editor/icon_font.h"
#include "editor/imgui_theme.h"
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
#include <chrono>
#include <filesystem>
#include <cstring>

namespace {

void save_active_document_with_existing_flow(WindowSystem& ws, Document* doc) {
    if (!doc) return;

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
        icon_font_.handle = reinterpret_cast<ui::IDrawList::NativeFont>(fa_font);
        ws_.renderingResources().icon_font = &icon_font_;
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

    if (ws_.settings.hasOpenTabs()) {
        const auto saved_tabs = ws_.settings.openTabs();
        bool had_failures = false;
        std::string failed_list;
        for (const auto& path : saved_tabs) {
            if (!ws_.openDocument(path)) {
                had_failures = true;
                if (!failed_list.empty()) failed_list += "\n";
                failed_list += path;
                ws_.settings.removeOpenTab(path);
            }
        }
        if (had_failures) {
            pending_open_error_.show = true;
            pending_open_error_.message = "Failed to open one or more tabs:\n" + failed_list;
        }
        if (!ws_.settings.activeTab().empty()) {
            if (Document* doc = ws_.findDocumentByPath(ws_.settings.activeTab())) {
                ws_.setActiveDocument(doc);
                ws_.setPendingTabFocus(doc);
            }
        }
    }

    prof_events_ = profiler_.register_section("handleEvents");
    prof_imgui_newframe_ = profiler_.register_section("ImGui NewFrame");
    prof_sim_step_ = profiler_.register_section("sim_step");
    prof_node_content_ = profiler_.register_section("update_node_content");
    prof_osc_blueprint_ = profiler_.register_section("osc_blueprint_changed");
    prof_osc_sample_ = profiler_.register_section("osc_sample");
    prof_render_menu_ = profiler_.register_section("render_menu");
    prof_render_inspector_ = profiler_.register_section("  inspector");
    prof_render_doc_area_ = profiler_.register_section("  document_area");
    prof_render_sub_windows_ = profiler_.register_section("  sub_windows");
    prof_render_osc_ = profiler_.register_section("  oscilloscope");
    prof_render_dialogs_ = profiler_.register_section("  dialogs/properties");
    prof_render_present_ = profiler_.register_section("render_present(gl+swap)");

    running_ = true;
    while (running_) {
        auto frame_t0 = std::chrono::steady_clock::now();

        { SCOPED_PROFILE(profiler_, prof_events_);
        handleEvents();
        }

        update();
        render();

        profiler_.add_frame(std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - frame_t0).count());
        profiler_.maybe_report();

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

    { SCOPED_PROFILE(profiler_, prof_imgui_newframe_);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    }

    if (Document* doc = ws_.activeDocument()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_S)) {
            save_active_document_with_existing_flow(ws_, doc);
        }
    }

    if (!io.WantCaptureKeyboard && !ws_.propertiesWindow().is_open()) {
        if (Document* doc = ws_.activeDocument()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (doc->isSimulationRunning()) doc->stopSimulation();
                else doc->startSimulation();
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_Z)) {
                doc->performUndo();
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z) ||
                ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_Y)) {
                doc->performRedo();
            }
        }
    }

    if (Document* doc = ws_.activeDocument()) {
        { SCOPED_PROFILE(profiler_, prof_sim_step_);
        doc->updateSimulationStep(io.DeltaTime);
        }

        { SCOPED_PROFILE(profiler_, prof_node_content_);
        doc->updateNodeContentFromSimulation();
        }

        { SCOPED_PROFILE(profiler_, prof_osc_blueprint_);
        ws_.oscilloscope.on_blueprint_changed(*doc);
        }

        { SCOPED_PROFILE(profiler_, prof_osc_sample_);
        ws_.oscilloscope.sample(*doc, doc->isSimulationRunning(), io.DeltaTime);
        }
    }
}

void EditorApp::render() {
    auto& io = ImGui::GetIO();

    { SCOPED_PROFILE(profiler_, prof_render_menu_);
    auto menu_result = main_menu_.render(ws_);
    if (menu_result.exit_requested) {
        running_ = false;
    }

    zn_tune_result_dialog_.render(ws_);

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
    }

    float menu_height = ImGui::GetFrameHeight();
    float available_h = io.DisplaySize.y - menu_height;
    float available_w = io.DisplaySize.x;

    { SCOPED_PROFILE(profiler_, prof_render_inspector_);
    inspector_panel_.setVisible(ws_.showInspector);

    if (inspector_panel_.visible()) {
        auto inspector_result = inspector_panel_.render(ws_, menu_height, available_h, available_w);
        if (!inspector_result.selected_node_id.empty() && ws_.activeDocument()) {
            ws_.activeDocument()->input().select_node_by_id(inspector_result.selected_node_id);
        }
        ws_.showInspector = inspector_panel_.visible();
    }
    }

    float canvas_x = inspector_panel_.totalWidth();

    ws_.reconcile_owner_bound_ui();

    { SCOPED_PROFILE(profiler_, prof_render_doc_area_);
    auto doc_result = document_area_.render(ws_, canvas_x, menu_height, available_w - canvas_x, available_h);
    if (doc_result.close_requested) {
        ws_.closeDocument(*doc_result.close_requested);
    }
    }

    { SCOPED_PROFILE(profiler_, prof_render_sub_windows_);
    sub_window_renderer_.renderAll(ws_);
    }

    { SCOPED_PROFILE(profiler_, prof_render_osc_);
    oscilloscope_window_.render(ws_);
    }

    { SCOPED_PROFILE(profiler_, prof_render_dialogs_);
    context_menus_.renderAddComponent(ws_);
    context_menus_.renderNodeContext(ws_);

    color_picker_.render(ws_);
    bake_in_dialog_.render(ws_);
    set_name_dialog_.render(ws_);
    extract_to_blueprint_dialog_.render(ws_);
    inline_value_editor_dialog_.render(ws_);
    ws_.scriptEditorWindow().render();

    ws_.propertiesWindow().render();
    }

    { SCOPED_PROFILE(profiler_, prof_render_present_);
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.078f, 0.082f, 0.102f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
    }
}
