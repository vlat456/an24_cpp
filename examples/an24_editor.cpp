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

    // Window system (manages multiple documents)
    WindowSystem ws;

    // Load default file into active document
    const char* default_file = "/Users/vladimir/an24_cpp/blueprint.json";
    if (Document* doc = ws.activeDocument()) {
        if (!doc->load(default_file)) {
            // File doesn't exist or failed to load - that's ok, start with empty blueprint
            DEBUG_INFO("Default file not found or failed to load: {}", default_file);
        } else {
            DEBUG_INFO("Loaded default file: {}", default_file);
        }
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

        // Меню
        if (ImGui::BeginMainMenuBar()) {
            Document* active_doc = ws.activeDocument();

            // Simulation indicator
            if (active_doc && active_doc->isSimulationRunning()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "▶ SIM");
            }

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    ws.createDocument();
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    nfdu8filteritem_t filterItem = {"Blueprint JSON", "json"};
                    nfdchar_t* outPath = nullptr;
                    nfdresult_t result = NFD_OpenDialog(&outPath, &filterItem, 1, nullptr);
                    if (result == NFD_OKAY) {
                        ws.openDocument(outPath);
                        NFD_FreePath(outPath);
                    }
                }
                if (ImGui::MenuItem("Save", "Ctrl+S", false, active_doc && active_doc->isModified())) {
                    if (active_doc) {
                        if (active_doc->filepath().empty()) {
                            nfdu8filteritem_t filterItem = {"Blueprint JSON", "json"};
                            nfdchar_t* outPath = nullptr;
                            nfdresult_t result = NFD_SaveDialog(&outPath, &filterItem, 1, nullptr, "blueprint.json");
                            if (result == NFD_OKAY) {
                                active_doc->save(outPath);
                                NFD_FreePath(outPath);
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
                    running = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Inspector", nullptr, ws.showInspector)) {
                    ws.showInspector = !ws.showInspector;
                }
                ImGui::Separator();
                if (active_doc) {
                    if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
                        active_doc->scene().viewport().zoom *= 1.1f;
                        active_doc->scene().viewport().clamp_zoom();
                    }
                    if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
                        active_doc->scene().viewport().zoom /= 1.1f;
                        active_doc->scene().viewport().clamp_zoom();
                    }
                    if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
                        active_doc->scene().viewport().zoom = 1.0f;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                bool has_sel = active_doc && !active_doc->input().selected_nodes().empty();
                if (ImGui::MenuItem("Delete", "Del", false, has_sel)) {
                    if (active_doc) {
                        auto action = active_doc->applyInputResult(active_doc->input().on_key(Key::Delete));
                        ws.handleInputAction(action, *active_doc);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ================================================================
        // Helper lambda: render + handle input for any BlueprintWindow.
        // Used identically for root canvas and sub-windows.
        // ================================================================
        auto process_window = [&](BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax,
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
                             &doc.simulation(), win.input.hovered_wire());

            // Tooltip
            if (hovered) {
                ImVec2 mp = ImGui::GetMousePos();
                Pt world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
                auto tooltip = win.scene.detectTooltip(world, doc.simulation(), cmin);
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
            for (auto& node : doc.blueprint().nodes) {
                if (node.group_id != win.scene.groupId()) continue;

                auto* visual = win.scene.cache().getOrCreate(node, doc.blueprint().wires);
                visual->setPosition(node.pos);
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
                                if (checked) doc.holdButtonPress(node.id);
                                else doc.holdButtonRelease(node.id);
                            }
                        }
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
            if (io.MouseWheel != 0.0f) {
                auto action = doc.applyInputResult(win.input.on_scroll(io.MouseWheel * 10.0f, screen_pos, cmin), win.group_id);
                ws.handleInputAction(action, doc);
            }

            // Double-click (before single click)
            bool was_dbl = false;
            if (ImGui::IsMouseDoubleClicked(0)) {
                auto action = doc.applyInputResult(win.input.on_double_click(screen_pos, cmin), win.group_id);
                ws.handleInputAction(action, doc);
                was_dbl = true;
            }

            // Left click
            if (!was_dbl && ImGui::IsMouseClicked(0)) {
                auto action = doc.applyInputResult(win.input.on_mouse_down(screen_pos, MouseButton::Left, cmin, mods), win.group_id);
                ws.handleInputAction(action, doc);
            }

            // Right click
            if (ImGui::IsMouseClicked(1)) {
                auto action = doc.applyInputResult(win.input.on_mouse_down(screen_pos, MouseButton::Right, cmin, mods), win.group_id);
                ws.handleInputAction(action, doc);
            }

            // Left drag
            if (ImGui::IsMouseDragging(0)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(0);
                auto action = doc.applyInputResult(win.input.on_mouse_drag(MouseButton::Left, Pt(delta.x, delta.y), cmin), win.group_id);
                ws.handleInputAction(action, doc);
                ImGui::ResetMouseDragDelta(0);
            }

            // Left release
            if (ImGui::IsMouseReleased(0)) {
                auto action = doc.applyInputResult(win.input.on_mouse_up(MouseButton::Left, screen_pos, cmin), win.group_id);
                ws.handleInputAction(action, doc);
            }

            // Keyboard (per-window: Delete/Backspace, Escape, R, brackets)
            if (!io.WantCaptureKeyboard) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::Escape), win.group_id);
                    ws.handleInputAction(action, doc);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::Delete), win.group_id);
                    ws.handleInputAction(action, doc);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::Backspace), win.group_id);
                    ws.handleInputAction(action, doc);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::R), win.group_id);
                    ws.handleInputAction(action, doc);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::LeftBracket), win.group_id);
                    ws.handleInputAction(action, doc);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                    auto action = doc.applyInputResult(win.input.on_key(Key::RightBracket), win.group_id);
                    ws.handleInputAction(action, doc);
                }
            }
        };

        // ================================================================
        // Inspector (docked left panel) + Root canvas with document tabs
        // ================================================================
        float menu_height = ImGui::GetFrameHeight();
        float available_h = io.DisplaySize.y - menu_height;
        static float inspector_width = 280.0f;
        const float splitter_thickness = 4.0f;
        const float min_inspector_w = 150.0f;
        const float max_inspector_w = io.DisplaySize.x * 0.5f;
        float canvas_x = 0.0f;

        // Left panel: Inspector
        if (ws.showInspector) {
            ImGui::SetNextWindowPos(ImVec2(0, menu_height));
            ImGui::SetNextWindowSize(ImVec2(inspector_width, available_h));
            ImGui::Begin("Inspector", &ws.showInspector,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse);
            ws.inspector().render();

            // Inspector click → select + center on canvas
            std::string sel = ws.inspector().consumeSelection();
            if (!sel.empty() && ws.activeDocument()) {
                ws.activeDocument()->input().selectNodeById(sel);
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

        // Root canvas with document tabs (fills remaining space to the right)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(canvas_x, menu_height));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - canvas_x, available_h));
        ImGui::Begin("##DocumentArea", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::BeginTabBar("DocumentTabs")) {
            for (auto& doc : ws.documents()) {
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (doc->isModified()) flags |= ImGuiTabItemFlags_UnsavedDocument;

                bool tab_open = true;
                std::string tab_label = doc->title() + "###" + doc->id();
                if (ImGui::BeginTabItem(tab_label.c_str(), &tab_open, flags)) {
                    // This tab is selected → active document
                    if (ws.activeDocument() != doc.get()) {
                        ws.setActiveDocument(doc.get());
                    }

                    // Render canvas for this document's root window
                    auto canvas_min = ImGui::GetWindowContentRegionMin();
                    auto canvas_max = ImGui::GetWindowContentRegionMax();
                    Pt cmin(canvas_min.x + ImGui::GetWindowPos().x,
                            canvas_min.y + ImGui::GetWindowPos().y);
                    Pt cmax(canvas_max.x + ImGui::GetWindowPos().x,
                            canvas_max.y + ImGui::GetWindowPos().y);
                    bool hovered = ImGui::IsWindowHovered();

                    process_window(doc->root(), *doc, cmin, cmax,
                                  ImGui::GetWindowDrawList(), hovered);

                    ImGui::EndTabItem();
                }
                if (!tab_open) {
                    // User clicked X on tab — close document
                    ws.closeDocument(*doc);
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // ================================================================
        // Sub-blueprint windows (one per open collapsed group, per document)
        // ================================================================
        for (auto& doc : ws.documents()) {
            doc->windowManager().removeClosedWindows();
            for (auto& win_ptr : doc->windowManager().windows()) {
                auto& win = *win_ptr;
                if (win.group_id.empty()) continue;
                if (!win.open) continue;

                ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
                // Include doc ID in window ID to prevent conflicts between documents
                std::string win_title = win.title + " [" + doc->displayName() + "]###"
                                       + doc->id() + ":" + win.group_id;
                if (!ImGui::Begin(win_title.c_str(), &win.open,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    ImGui::End();
                    continue;
                }

                // Toolbar: Fit View + Auto Layout + Delete
                if (ImGui::Button("Fit View")) {
                    Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
                    for (const auto& node : doc->blueprint().nodes) {
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
                    doc->blueprint().auto_layout_group(win.group_id);
                    win.scene.clearCache();
                    Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
                    for (const auto& node : doc->blueprint().nodes) {
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
                        auto action = doc->applyInputResult(win.input.on_key(Key::Delete), win.group_id);
                        ws.handleInputAction(action, *doc);
                    }
                    if (!has_sel) ImGui::EndDisabled();
                }

                // InvisibleButton captures mouse input in content area
                ImVec2 content_size = ImGui::GetContentRegionAvail();
                ImGui::InvisibleButton(("##canvas_" + doc->id() + "_" + win.group_id).c_str(), content_size);
                bool sub_hovered = ImGui::IsItemHovered();

                auto sub_cmin = ImGui::GetWindowContentRegionMin();
                auto sub_cmax = ImGui::GetWindowContentRegionMax();
                Pt sub_min(sub_cmin.x + ImGui::GetWindowPos().x, sub_cmin.y + ImGui::GetWindowPos().y);
                Pt sub_max(sub_cmax.x + ImGui::GetWindowPos().x, sub_cmax.y + ImGui::GetWindowPos().y);

                process_window(win, *doc, sub_min, sub_max, ImGui::GetWindowDrawList(), sub_hovered);

                ImGui::End();
            }
        }

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
                        ImVec2 mp = ImGui::GetMousePosOnOpeningCurrentPopup();
                        Pt menu_pos(mp.x, mp.y);
                        Document* doc = ws.contextMenu.source_doc ? ws.contextMenu.source_doc : ws.activeDocument();
                        if (doc) {
                            Pt world_pos = doc->scene().viewport().screen_to_world(menu_pos, Pt(canvas_x, menu_height));
                            doc->addComponent(classname, world_pos, ws.contextMenu.group_id, ws.typeRegistry());
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
                if (ImGui::MenuItem("Properties...")) {
                    ws.openPropertiesForNode(ws.nodeContextMenu.node_index, *doc);
                }
                if (ImGui::MenuItem("Set Color...")) {
                    ws.openColorPickerForNode(ws.nodeContextMenu.node_index, ws.nodeContextMenu.group_id, *doc);
                }
                if (ImGui::MenuItem("Delete")) {
                    auto action = doc->applyInputResult(doc->input().on_key(Key::Delete), ws.nodeContextMenu.group_id);
                    ws.handleInputAction(action, *doc);
                }
            }
            ImGui::EndPopup();
        }

        // Color picker dialog
        if (ws.colorPicker.show) {
            ImGui::OpenPopup("Node Color");
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
                    doc->markModified();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    Node& node = doc->blueprint().nodes[ws.colorPicker.node_index];
                    node.color = std::nullopt;
                    auto* vn = target_scene->getVisualNode(ws.colorPicker.node_index);
                    if (vn) vn->setCustomColor(std::nullopt);
                    doc->markModified();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
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
