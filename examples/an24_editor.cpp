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

#include "editor/app.h"
#include "editor/window/window_manager.h"
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/visual/scene/persist.h"
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

    // Приложение редактора
    EditorApp app;

    // Загружаем файл по умолчанию
    const char* default_file = "/Users/vladimir/an24_cpp/blueprint.json";
    if (!app.scene.load(default_file)) {
        // File doesn't exist or failed to load - that's ok, start with empty blueprint
        DEBUG_INFO("Default file not found or failed to load: {}", default_file);
    } else {
        DEBUG_INFO("Loaded default file: {}", default_file);
    }

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
            // Space toggles simulation (app-level, not per-window)
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (app.simulation_running) app.stop_simulation();
                else app.start_simulation();
            }
            // Per-window keys are dispatched inside process_window (below)
        }

        // Обновление симуляции каждый кадр
        app.update_simulation_step(io.DeltaTime);

        // Обновление node_content на основе значений симуляции
        app.update_node_content_from_simulation();

        // Меню
        if (ImGui::BeginMainMenuBar()) {
            // Simulation indicator
            if (app.simulation_running) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "▶ SIM");
            }

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    app.new_circuit();
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    nfdu8filteritem_t filterItem = {"Blueprint JSON", "json"};
                    nfdchar_t* outPath = nullptr;
                    nfdresult_t result = NFD_OpenDialog(&outPath, &filterItem, 1, nullptr);
                    if (result == NFD_OKAY) {
                        if (app.scene.load(outPath)) {
                            DEBUG_INFO("Loaded file: {}", outPath);
                        } else {
                            DEBUG_ERROR("Failed to load file: {}", outPath);
                        }
                        NFD_FreePath(outPath);
                    }
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    nfdu8filteritem_t filterItem = {"Blueprint JSON", "json"};
                    nfdchar_t* outPath = nullptr;
                    nfdresult_t result = NFD_SaveDialog(&outPath, &filterItem, 1, nullptr, "blueprint.json");
                    if (result == NFD_OKAY) {
                        app.scene.save(outPath);
                        NFD_FreePath(outPath);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Inspector", nullptr, app.show_inspector)) {
                    app.show_inspector = !app.show_inspector;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
                    app.scene.viewport().zoom *= 1.1f;
                    app.scene.viewport().clamp_zoom(); // [d4e5f6g7]
                }
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
                    app.scene.viewport().zoom /= 1.1f;
                    app.scene.viewport().clamp_zoom(); // [d4e5f6g7]
                }
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
                    app.scene.viewport().zoom = 1.0f;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                bool has_sel = !app.input.selected_nodes().empty();
                if (ImGui::MenuItem("Delete", "Del", false, has_sel)) {
                    app.apply_input_result(app.input.on_key(Key::Delete));
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ================================================================
        // Helper lambda: render + handle input for any BlueprintWindow.
        // Used identically for root canvas and sub-windows.
        // ================================================================
        auto process_window = [&](BlueprintWindow& win, Pt cmin, Pt cmax,
                                  ImDrawList* draw_list, bool hovered)
        {
            ImGuiDrawList dl;
            dl.dl = draw_list;

            // Update hover state (needed for wire hover highlighting)
            if (hovered) {
                ImVec2 mp = ImGui::GetMousePos();
                Pt mouse_world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
                win.input.update_hover(mouse_world);
            } else {
                win.input.update_hover(Pt(-1e9f, -1e9f));  // clear hover
            }

            // Grid + blueprint
            BlueprintRenderer::renderGrid(dl, win.scene.viewport(), cmin, cmax);
            win.scene.render(dl, cmin, cmax,
                             &win.input.selected_nodes(), win.input.selected_wire(),
                             &app.simulation, win.input.hovered_wire());

            // Tooltip
            if (hovered) {
                ImVec2 mp = ImGui::GetMousePos();
                Pt world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
                auto tooltip = win.scene.detectTooltip(world, app.simulation, cmin);
                BlueprintRenderer::renderTooltip(dl, tooltip);
            }

            // Temporary wire while creating/reconnecting
            if (win.input.has_temp_wire()) {
                Pt start_world = win.input.temp_wire_start();
                Pt end_world = win.input.temp_wire_end_world();
                Pt s = win.scene.viewport().world_to_screen(start_world, cmin);
                Pt e = win.scene.viewport().world_to_screen(end_world, cmin);
                uint32_t color = win.input.is_reconnecting()
                    ? IM_COL32(255, 150, 50, 200)
                    : IM_COL32(255, 255, 100, 200);
                dl.dl->AddLine(ImVec2(s.x, s.y), ImVec2(e.x, e.y), color, 2.0f);
            }

            // Node content widgets
            for (auto& node : app.blueprint.nodes) {
                if (node.group_id != win.scene.groupId()) continue;

                auto* visual = win.scene.cache().getOrCreate(node, app.blueprint.wires);
                visual->setPosition(node.pos);
                // Sync data model FROM auto-sized visual (not the other way around).
                // VisualNode constructor computes correct size via layout preferred size;
                // overwriting with stale Node.size would clip content (gauges, ports, etc).
                node.size = visual->getSize();

                Pt screen_min = win.scene.viewport().world_to_screen(visual->getPosition(), cmin);
                float zoom = win.scene.viewport().zoom;
                NodeContentType ctype = visual->getContentType();
                if (ctype == NodeContentType::None) continue;

                Bounds cb = visual->getContentBounds();
                float cx = screen_min.x + cb.x * zoom;
                float cy = screen_min.y + cb.y * zoom;
                float aw = cb.w * zoom;
                if (aw <= 20.0f) continue;

                ImGui::SetCursorScreenPos(ImVec2(cx, cy));
                NodeContent& content = node.node_content;

                switch (content.type) {
                    case NodeContentType::Switch: {
                        if (node.type_name == "HoldButton") {
                            bool checked = content.state;
                            std::string id = "##hold_" + node.id;
                            if (ImGui::Checkbox(id.c_str(), &checked)) {
                                if (checked) app.hold_button_press(node.id);
                                else app.hold_button_release(node.id);
                            }
                        }
                        // Other Switch/AZS nodes: visual drawn by SwitchWidget,
                        // clicks handled via CanvasInput hit testing (no ImGui overlay)
                        break;
                    }
                    case NodeContentType::Value: {
                        ImGui::SetNextItemWidth(aw);
                        std::string id = "##v_" + node.id;
                        ImGui::SliderFloat(id.c_str(), &content.value, content.min, content.max, "%.2f");
                        break;
                    }
                    case NodeContentType::Gauge: {
                        float range = content.max - content.min;
                        float progress = (range > 1e-6f) ? (content.value - content.min) / range : 0.0f;
                        progress = std::max(0.0f, std::min(1.0f, progress));
                        ImGui::ProgressBar(progress, ImVec2(aw, 0));
                        break;
                    }
                    case NodeContentType::Text:
                        ImGui::Text("%s", content.label.c_str());
                        break;
                    default: break;
                }
            }

            // Marquee selection rectangle
            if (win.input.is_marquee_selecting()) {
                Pt ms = win.scene.viewport().world_to_screen(win.input.marquee_start(), cmin);
                Pt me = win.scene.viewport().world_to_screen(win.input.marquee_end(), cmin);
                Pt rmin(std::min(ms.x, me.x), std::min(ms.y, me.y));
                Pt rmax(std::max(ms.x, me.x), std::max(ms.y, me.y));
                dl.add_rect_filled(rmin, rmax, 0x4000FF00);
                dl.add_rect(rmin, rmax, 0xFF00FF00, 1.0f);
            }

            // ---- Input handling (unified FSM) ----
            if (!hovered) return;

            Modifiers mods;
            mods.alt  = io.KeyAlt;
            mods.ctrl = io.KeyCtrl || io.KeySuper;

            ImVec2 mp = ImGui::GetMousePos();
            Pt screen_pos(mp.x, mp.y);

            // Scroll → zoom
            if (io.MouseWheel != 0.0f)
                app.apply_input_result(win.input.on_scroll(io.MouseWheel * 10.0f, screen_pos, cmin), win.group_id);

            // Double-click (before single click)
            bool was_dbl = false;
            if (ImGui::IsMouseDoubleClicked(0)) {
                app.apply_input_result(win.input.on_double_click(screen_pos, cmin), win.group_id);
                was_dbl = true;
            }

            // Left click
            if (!was_dbl && ImGui::IsMouseClicked(0))
                app.apply_input_result(win.input.on_mouse_down(screen_pos, MouseButton::Left, cmin, mods), win.group_id);

            // Right click
            if (ImGui::IsMouseClicked(1))
                app.apply_input_result(win.input.on_mouse_down(screen_pos, MouseButton::Right, cmin, mods), win.group_id);

            // Left drag
            if (ImGui::IsMouseDragging(0)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(0);
                app.apply_input_result(win.input.on_mouse_drag(MouseButton::Left, Pt(delta.x, delta.y), cmin), win.group_id);
                ImGui::ResetMouseDragDelta(0);
            }

            // Left release
            if (ImGui::IsMouseReleased(0))
                app.apply_input_result(win.input.on_mouse_up(MouseButton::Left, screen_pos, cmin), win.group_id);

            // Keyboard (per-window: Delete/Backspace, Escape, R, brackets)
            if (!io.WantCaptureKeyboard) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    app.apply_input_result(win.input.on_key(Key::Escape), win.group_id);
                if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                    app.apply_input_result(win.input.on_key(Key::Delete), win.group_id);
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
                    app.apply_input_result(win.input.on_key(Key::Backspace), win.group_id);
                if (ImGui::IsKeyPressed(ImGuiKey_R))
                    app.apply_input_result(win.input.on_key(Key::R), win.group_id);
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
                    app.apply_input_result(win.input.on_key(Key::LeftBracket), win.group_id);
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
                    app.apply_input_result(win.input.on_key(Key::RightBracket), win.group_id);
            }
        };

        // ================================================================
        // Inspector (docked left panel) + Root canvas
        // ================================================================
        float menu_height = ImGui::GetFrameHeight();
        float available_h = io.DisplaySize.y - menu_height;
        static float inspector_width = 280.0f;
        const float splitter_thickness = 4.0f;
        const float min_inspector_w = 150.0f;
        const float max_inspector_w = io.DisplaySize.x * 0.5f;
        float canvas_x = 0.0f;

        // Left panel: Inspector
        if (app.show_inspector) {
            ImGui::SetNextWindowPos(ImVec2(0, menu_height));
            ImGui::SetNextWindowSize(ImVec2(inspector_width, available_h));
            ImGui::Begin("Inspector", &app.show_inspector,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse);
            app.inspector.render();

            // Inspector click → select + center on canvas
            std::string sel = app.inspector.consumeSelection();
            if (!sel.empty()) {
                app.input.selectNodeById(sel);
            }

            ImGui::End();

            // Splitter (invisible draggable button between inspector and canvas)
            ImGui::SetNextWindowPos(ImVec2(inspector_width, menu_height));
            ImGui::SetNextWindowSize(ImVec2(splitter_thickness, available_h));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(splitter_thickness, splitter_thickness));
            ImGui::Begin("##Splitter", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::InvisibleButton("##splitter_btn", ImVec2(splitter_thickness, available_h));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive()) {
                inspector_width += io.MouseDelta.x;
                if (inspector_width < min_inspector_w) inspector_width = min_inspector_w;
                if (inspector_width > max_inspector_w) inspector_width = max_inspector_w;
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            canvas_x = inspector_width + splitter_thickness;
        }

        // Root canvas (fills remaining space to the right)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(canvas_x, menu_height));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - canvas_x, available_h));
        ImGui::Begin("Canvas", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        auto canvas_min = ImGui::GetWindowContentRegionMin();
        auto canvas_max = ImGui::GetWindowContentRegionMax();
        Pt canvas_min_pt(canvas_min.x + ImGui::GetWindowPos().x, canvas_min.y + ImGui::GetWindowPos().y);
        Pt canvas_max_pt(canvas_max.x + ImGui::GetWindowPos().x, canvas_max.y + ImGui::GetWindowPos().y);
        bool root_hovered = ImGui::IsWindowHovered();

        process_window(app.window_manager.root(), canvas_min_pt, canvas_max_pt,
                        ImGui::GetWindowDrawList(), root_hovered);

        ImGui::End();
        ImGui::PopStyleVar();

        // ================================================================
        // Sub-blueprint windows (one per open collapsed group)
        // ================================================================
        app.window_manager.removeClosedWindows();
        for (auto& win_ptr : app.window_manager.windows()) {
            auto& win = *win_ptr;
            if (win.group_id.empty()) continue;
            if (!win.open) continue;

            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            std::string win_title = win.title + "###" + win.group_id;
            if (!ImGui::Begin(win_title.c_str(), &win.open,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::End();
                continue;
            }

            // Toolbar: Fit View + Auto Layout + Delete
            if (ImGui::Button("Fit View")) {
                Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
                for (const auto& node : app.blueprint.nodes) {
                    if (node.group_id != win.group_id) continue;
                    bmin.x = std::min(bmin.x, node.pos.x);
                    bmin.y = std::min(bmin.y, node.pos.y);
                    bmax.x = std::max(bmax.x, node.pos.x + node.size.x);
                    bmax.y = std::max(bmax.y, node.pos.y + node.size.y);
                }
                if (bmin.x < bmax.x && bmin.y < bmax.y) {
                    ImVec2 ws = ImGui::GetContentRegionAvail();
                    win.scene.viewport().fit_content(bmin, bmax, ws.x, ws.y);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Auto Layout")) {
                app.blueprint.auto_layout_group(win.group_id);
                win.scene.clearCache();
                // Fit view after layout
                Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
                for (const auto& node : app.blueprint.nodes) {
                    if (node.group_id != win.group_id) continue;
                    bmin.x = std::min(bmin.x, node.pos.x);
                    bmin.y = std::min(bmin.y, node.pos.y);
                    bmax.x = std::max(bmax.x, node.pos.x + node.size.x);
                    bmax.y = std::max(bmax.y, node.pos.y + node.size.y);
                }
                if (bmin.x < bmax.x && bmin.y < bmax.y) {
                    ImVec2 ws = ImGui::GetContentRegionAvail();
                    win.scene.viewport().fit_content(bmin, bmax, ws.x, ws.y);
                }
            }
            ImGui::SameLine();
            {
                bool has_sel = !win.input.selected_nodes().empty();
                if (!has_sel) ImGui::BeginDisabled();
                if (ImGui::Button("Delete")) {
                    app.apply_input_result(win.input.on_key(Key::Delete), win.group_id);
                }
                if (!has_sel) ImGui::EndDisabled();
            }

            // InvisibleButton captures mouse input in content area
            ImVec2 content_size = ImGui::GetContentRegionAvail();
            ImGui::InvisibleButton(("##canvas_" + win.group_id).c_str(), content_size);
            bool sub_hovered = ImGui::IsItemHovered();

            auto sub_cmin = ImGui::GetWindowContentRegionMin();
            auto sub_cmax = ImGui::GetWindowContentRegionMax();
            Pt sub_min(sub_cmin.x + ImGui::GetWindowPos().x, sub_cmin.y + ImGui::GetWindowPos().y);
            Pt sub_max(sub_cmax.x + ImGui::GetWindowPos().x, sub_cmax.y + ImGui::GetWindowPos().y);

            process_window(win, sub_min, sub_max, ImGui::GetWindowDrawList(), sub_hovered);

            ImGui::End();
        }

        // Context menu for adding components (right-click on empty space)
        if (app.show_context_menu) {
            ImGui::OpenPopup("AddComponent");
            app.show_context_menu = false; // Reset flag
        }

        if (ImGui::BeginPopup("AddComponent")) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Add Component");
            ImGui::Separator();

            // Render hierarchical menu from library/ directory structure
            auto menu_tree = app.type_registry.build_menu_tree();
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
                        app.add_component(classname, app.context_menu_pos, app.context_menu_group_id);
                    }
                }
            };
            render_menu(menu_tree);

            ImGui::EndPopup();
        }

        // Node context menu (right-click on node)
        if (app.show_node_context_menu) {
            ImGui::OpenPopup("NodeContextMenu");
            app.show_node_context_menu = false;
        }
        if (ImGui::BeginPopup("NodeContextMenu")) {
            if (ImGui::MenuItem("Color...")) {
                app.open_color_picker_for_node(app.context_menu_node_index);
            }
            if (ImGui::MenuItem("Properties")) {
                app.open_properties_for_node(app.context_menu_node_index);
            }
            ImGui::EndPopup();
        }

        // Color picker dialog
        if (app.show_color_picker) {
            ImGui::OpenPopup("Node Color");
        }
        if (ImGui::BeginPopupModal("Node Color", &app.show_color_picker, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::ColorPicker4("##picker", app.color_picker_rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

            // Find the correct window's scene for visual cache update
            auto* target_win = app.window_manager.find(app.color_picker_group_id);
            VisualScene* target_scene = target_win ? &target_win->scene : &app.scene;

            if (ImGui::Button("Apply")) {
                Node& node = app.blueprint.nodes[app.color_picker_node_index];
                node.color = NodeColor{
                    app.color_picker_rgba[0],
                    app.color_picker_rgba[1],
                    app.color_picker_rgba[2],
                    app.color_picker_rgba[3]
                };
                // Update VisualNode in the correct window's cache
                auto* vn = target_scene->getVisualNode(app.color_picker_node_index);
                if (vn) vn->setCustomColor(node.color);
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                Node& node = app.blueprint.nodes[app.color_picker_node_index];
                node.color = std::nullopt;
                auto* vn = target_scene->getVisualNode(app.color_picker_node_index);
                if (vn) vn->setCustomColor(std::nullopt);
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Properties window
        app.properties_window.render();

        // Рендер
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.078f, 0.082f, 0.102f, 1.0f);  // Canvas #14151A
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

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
