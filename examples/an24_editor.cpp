/**
 * AN-24 Blueprint Editor - imgui integration
 *
 * Компиляция:
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_BUILD_TYPE=Debug
 *   cmake --build .
 *
 * Запуск:
 *   ./examples/an24_editor
 *
 * Требования:
 *   - SDL2
 *   - OpenGL
 *   - imgui
 */

#include "editor/window_system.h"
#include "editor/window/window_manager.h"
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/visual/scene/persist.h"
#include "editor/visual/canvas_renderer.h"
#include "editor/visual/panels/inspector_panel.h"
#include "editor/visual/panels/document_area.h"
#include "editor/visual/windows/sub_window_renderer.h"
#include "editor/visual/menu/main_menu.h"
#include "editor/gl_setup.h"
#include "editor/data/blueprint.h"
#include "editor/imgui_theme.h"
#include "editor/imgui_draw_list.h"
#include "debug.h"

// DEBUG: включить для отладки событий мыши
#define DEBUG_MOUSE_EVENTS
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL2/SDL.h>
#include <nfd.h>

// OpenGL includes
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <cstdio>
#include <algorithm>
#include <functional>
#include <filesystem>

/// Get config file path (platform-aware)
static std::string getConfigPath() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) return std::string(appdata) + "/an24/recent_files.cfg";
    return "C:/an24/recent_files.cfg";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/Library/Application Support/an24/recent_files.cfg";
    return "/tmp/an24/recent_files.cfg";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/an24/recent_files.cfg";
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.config/an24/recent_files.cfg";
    return "/tmp/an24/recent_files.cfg";
#endif
}

/// Ensure parent directory exists
static void ensureConfigDir(const std::string& path) {
    auto dir = std::filesystem::path(path).parent_path();
    std::filesystem::create_directories(dir);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    DEBUG_INFO("Starting AN-24 Blueprint Editor");

    // SDL2 инициализация
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // OpenGL 3.2 Core Profile — минимальная версия для macOS
    // macOS не поддерживает legacy GLSL (120, 130), только Core Profile 3.2+
    // Соответствующий шейдерный язык: GLSL 150
    const char* glsl_version = gl_setup::GLSL_VERSION;
    if (gl_setup::FORWARD_COMPAT) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    }
    if (gl_setup::CORE_PROFILE) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_setup::GL_MAJOR);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_setup::GL_MINOR);

    // Depth / Stencil
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, gl_setup::DOUBLE_BUFFER);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, gl_setup::DEPTH_SIZE);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, gl_setup::STENCIL_SIZE);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_Window* window = SDL_CreateWindow("AN-24 Blueprint Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900, window_flags);
    if (!window) {
        printf("Error creating window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        printf("Error creating GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // VSync

    printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("GLSL version:   %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    // ImGui инициализация
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.IniFilename = nullptr; // Не сохраняем imgui.ini

    // === MODERN THEME WITH ROBOTO FONT ===
    ImGuiTheme::LoadRobotoWithCyrillic(18.0f);  // Load Roboto with Russian support
    ImGuiTheme::ApplyModernDarkTheme();           // Apply modern dark theme
    // ============================================

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Window system (manages multiple documents)
    WindowSystem ws;
    an24::InspectorPanel inspector_panel;
    an24::DocumentArea document_area;
    an24::MainMenu main_menu;
    an24::SubWindowRenderer sub_window_renderer;
    
    // Load recent files
    std::string recent_cfg = getConfigPath();
    ensureConfigDir(recent_cfg);
    ws.recent_files.loadFrom(recent_cfg);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }

            // Клавиатура через ImGui - после ImGui::NewFrame()
        }

        // ImGui новый кадр
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Обработка клавиатуры через ImGui
        if (!io.WantCaptureKeyboard) {
            // Space toggles simulation (active document only)
            if (Document* doc = ws.activeDocument()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                    if (doc->isSimulationRunning()) doc->stopSimulation();
                    else doc->startSimulation();
                }
            }
            // Per-window keys are dispatched inside process_window (below)
        }

        // Обновление симуляции каждый кадр (active document only)
        if (Document* doc = ws.activeDocument()) {
            doc->updateSimulationStep(io.DeltaTime);
            doc->updateNodeContentFromSimulation();
        }

        // Main menu
        auto menu_result = main_menu.render(ws);
        if (menu_result.exit_requested) {
            running = false;
        }

        // Inspector + Document Area layout
        float menu_height = ImGui::GetFrameHeight();
        float available_h = io.DisplaySize.y - menu_height;
        float available_w = io.DisplaySize.x;
        
        inspector_panel.setVisible(ws.showInspector);
        
        // Inspector panel (left side)
        if (inspector_panel.visible()) {
            auto inspector_result = inspector_panel.render(ws, menu_height, available_h, available_w);
            if (!inspector_result.selected_node_id.empty() && ws.activeDocument()) {
                ws.activeDocument()->input().selectNodeById(inspector_result.selected_node_id);
            }
            ws.showInspector = inspector_panel.visible();
        }
        
        float canvas_x = inspector_panel.totalWidth();
        
        // Document area (tabs + canvas)
        auto doc_result = document_area.render(ws, canvas_x, menu_height, 
                                                available_w - canvas_x, available_h);
        if (doc_result.close_requested) {
            ws.closeDocument(*doc_result.close_requested);
        }

        // Sub-blueprint windows
        sub_window_renderer.renderAll(ws);

        // Context menu for adding components (right-click on empty space)
        if (ws.contextMenu.show) {
            ImGui::OpenPopup("AddComponent");
            ws.contextMenu.show = false;
        }

        if (ImGui::BeginPopup("AddComponent")) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Add Component");
            ImGui::Separator();

            // Render hierarchical menu from library/ directory structure
            auto menu_tree = ws.typeRegistry().build_menu_tree();
            std::function<void(const an24::MenuTree&)> render_menu;
            render_menu = [&](const an24::MenuTree& tree) {
                for (const auto& [folder, subtree] : tree.children) {
                    if (ImGui::BeginMenu(folder.c_str())) {
                        render_menu(subtree);
                        ImGui::EndMenu();
                    }
                }
                for (const auto& classname : tree.entries) {
                    if (ImGui::MenuItem(classname.c_str())) {
                        Document* doc = ws.contextMenu.source_doc ? ws.contextMenu.source_doc : ws.activeDocument();
                        if (doc) {
                            doc->addComponent(classname, ws.contextMenu.position, ws.contextMenu.group_id, ws.typeRegistry());
                        }
                    }
                }
            };
            render_menu(menu_tree);

            ImGui::EndPopup();
        }

        // Node context menu (right-click on node)
        if (ws.nodeContextMenu.show) {
            ImGui::OpenPopup("NodeContextMenu");
            ws.nodeContextMenu.show = false;
        }
        if (ImGui::BeginPopup("NodeContextMenu")) {
            Document* doc = ws.nodeContextMenu.source_doc ? ws.nodeContextMenu.source_doc : ws.activeDocument();
            if (doc && ws.nodeContextMenu.node_index < doc->blueprint().nodes.size()) {
                Node& node = doc->blueprint().nodes[ws.nodeContextMenu.node_index];
                ImGui::Text("Node: %s", node.name.c_str());
                ImGui::Separator();
                
                // Check if this is a read-only window
                bool is_read_only = false;
                if (!ws.nodeContextMenu.group_id.empty()) {
                    BlueprintWindow* win = doc->windowManager().find(ws.nodeContextMenu.group_id);
                    is_read_only = win && win->read_only;
                }
                
                if (ImGui::MenuItem("Properties...")) {
                    ws.openPropertiesForNode(ws.nodeContextMenu.node_index, *doc);
                }
                if (!is_read_only) {
                    if (ImGui::MenuItem("Set Color...")) {
                        ws.openColorPickerForNode(ws.nodeContextMenu.node_index, ws.nodeContextMenu.group_id, *doc);
                    }
                    if (ImGui::MenuItem("Delete")) {
                        auto action = doc->applyInputResult(doc->input().on_key(Key::Delete), ws.nodeContextMenu.group_id);
                        ws.handleInputAction(action, *doc);
                    }
                }
                // Show "Bake In" / "Edit Original" for non-baked-in sub-blueprints.
                // Two cases: (1) right-click inside a sub-window (group_id is the SBI id),
                //            (2) right-click a collapsed composite node at root (node.id is the SBI id).
                {
                    const std::string& sbi_id = !ws.nodeContextMenu.group_id.empty()
                        ? ws.nodeContextMenu.group_id
                        : node.id;
                    auto* sb = doc->blueprint().find_sub_blueprint_instance(sbi_id);
                    if (sb && !sb->baked_in) {
                        if (ImGui::MenuItem("Bake In (Embed)")) {
                            ws.pendingBakeIn.show_confirmation = true;
                            ws.pendingBakeIn.sub_blueprint_id = sbi_id;
                            ws.pendingBakeIn.doc = ws.nodeContextMenu.source_doc ? ws.nodeContextMenu.source_doc : doc;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Edit Original")) {
                            std::string lib_path = "library/" + sb->blueprint_path + ".json";
                            ws.openDocument(lib_path);
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        // Color picker dialog
        if (ws.colorPicker.show) {
            ImGui::OpenPopup("Node Color");
            ws.colorPicker.show = false;
        }
        if (ImGui::BeginPopupModal("Node Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::ColorPicker4("##picker", ws.colorPicker.rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

            Document* doc = ws.colorPicker.source_doc ? ws.colorPicker.source_doc : ws.activeDocument();
            if (doc && ws.colorPicker.node_index < doc->blueprint().nodes.size()) {
                // Find the correct window's scene for visual cache update
                BlueprintWindow* target_win = nullptr;
                if (!ws.colorPicker.group_id.empty()) {
                    target_win = doc->windowManager().find(ws.colorPicker.group_id);
                }
                VisualScene* target_scene = target_win ? &target_win->scene : &doc->scene();

                if (ImGui::Button("Apply")) {
                    Node& node = doc->blueprint().nodes[ws.colorPicker.node_index];
                    node.color = NodeColor{
                        ws.colorPicker.rgba[0],
                        ws.colorPicker.rgba[1],
                        ws.colorPicker.rgba[2],
                        ws.colorPicker.rgba[3]
                    };
                    auto* vn = target_scene->getVisualNode(ws.colorPicker.node_index);
                    if (vn) vn->setCustomColor(node.color);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    Node& node = doc->blueprint().nodes[ws.colorPicker.node_index];
                    node.color = std::nullopt;
                    auto* vn = target_scene->getVisualNode(ws.colorPicker.node_index);
                    if (vn) vn->setCustomColor(std::nullopt);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        // Bake In confirmation dialog
        if (ws.pendingBakeIn.show_confirmation) {
            ImGui::OpenPopup("Bake In Confirmation");
            ws.pendingBakeIn.show_confirmation = false;
        }
        if (ImGui::BeginPopupModal("Bake In Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to bake in this sub-blueprint?");
            ImGui::Text("This will embed all nodes from the library file directly into this document.");
            ImGui::Separator();
            if (ImGui::Button("Bake In")) {
                if (ws.pendingBakeIn.doc) {
                    ws.pendingBakeIn.doc->blueprint().bake_in_sub_blueprint(ws.pendingBakeIn.sub_blueprint_id);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Properties window
        ws.propertiesWindow().render();

        // Рендер
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.078f, 0.082f, 0.102f, 1.0f);  // Canvas #14151A
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    // Save recent files before exit
    ws.recent_files.saveTo(recent_cfg);

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    DEBUG_INFO("Editor closed");

    return 0;
}
