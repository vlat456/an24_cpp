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
#include "editor/render.h"
#include "editor/persist.h"
#include "editor/gl_setup.h"
#include "editor/data/blueprint.h"
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

// Обертка IDrawList для imgui
class ImGuiDrawList : public IDrawList {
public:
    ImDrawList* dl = nullptr;

    void set_clip_rect(Pt min, Pt max) {
        ImGui::PushClipRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), true);
    }

    void clear_clip() {
        ImGui::PopClipRect();
    }

    void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), c, thickness);
    }

    void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c, 0, 0, thickness);
    }

    void add_rect_filled(Pt min, Pt max, uint32_t color) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), c);
    }

    void add_circle(Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircle(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        dl->AddCircleFilled(ImVec2(center.x, center.y), radius, c, segments);
    }

    void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) override {
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, font_size, ImVec2(pos.x, pos.y), c, text);
    }

    Pt calc_text_size(const char* text, float font_size) const override {
        ImFont* font = ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, FLT_MAX, text);
        return Pt(size.x, size.y);
    }

    void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) override {
        if (count < 2) return;
        ImU32 c = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF,
                           (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        ImVector<ImVec2> im_points;
        im_points.resize(count);
        for (size_t i = 0; i < count; i++) {
            im_points[i] = ImVec2(points[i].x, points[i].y);
        }
        dl->AddPolyline(im_points.Data, (int)count, c, false, thickness);
    }
};

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

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Приложение редактора
    EditorApp app;

    // Загружаем файл по умолчанию
    const char* default_file = "/Users/vladimir/an24_cpp/blueprint.json";
    auto bp = load_blueprint_from_file(default_file);
    if (bp.has_value()) {
        app.blueprint = std::move(*bp);
        app.viewport.pan = app.blueprint.pan;
        app.viewport.zoom = app.blueprint.zoom;
        app.viewport.grid_step = app.blueprint.grid_step;
        app.viewport.clamp_zoom();
        // Clear visual cache to rebuild nodes with correct node_content
        app.visual_cache.clear();
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
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                app.on_key_down(Key::Escape);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                app.on_key_down(Key::Delete);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                app.on_key_down(Key::R);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                app.on_key_down(Key::Space);
            }
        }

        // Обновление симуляции каждый кадр
        app.update_simulation_step();

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
                        auto bp = load_blueprint_from_file(outPath);
                        if (bp.has_value()) {
                            app.blueprint = std::move(*bp);
                            // Restore viewport from loaded blueprint
                            app.viewport.pan = app.blueprint.pan;
                            app.viewport.zoom = app.blueprint.zoom;
                            app.viewport.grid_step = app.blueprint.grid_step;
                            app.viewport.clamp_zoom();
                            // Clear visual cache to rebuild nodes with correct node_content
                            app.visual_cache.clear();
                        }
                        NFD_FreePath(outPath);
                    }
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    // Sync viewport to blueprint before saving
                    app.blueprint.pan = app.viewport.pan;
                    app.blueprint.zoom = app.viewport.zoom;
                    app.blueprint.grid_step = app.viewport.grid_step;
                    nfdu8filteritem_t filterItem = {"Blueprint JSON", "json"};
                    nfdchar_t* outPath = nullptr;
                    nfdresult_t result = NFD_SaveDialog(&outPath, &filterItem, 1, nullptr, "blueprint.json");
                    if (result == NFD_OKAY) {
                        save_blueprint_to_file(app.blueprint, outPath);
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
                if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
                    app.viewport.zoom *= 1.1f;
                }
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
                    app.viewport.zoom /= 1.1f;
                }
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
                    app.viewport.zoom = 1.0f;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Canvas область - на весь экран под меню
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        float menu_height = ImGui::GetFrameHeight();
        ImGui::SetNextWindowPos(ImVec2(0, menu_height));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menu_height));
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        auto canvas_min = ImGui::GetWindowContentRegionMin();
        auto canvas_max = ImGui::GetWindowContentRegionMax();
        Pt canvas_min_pt(canvas_min.x + ImGui::GetWindowPos().x, canvas_min.y + ImGui::GetWindowPos().y);
        Pt canvas_max_pt(canvas_max.x + ImGui::GetWindowPos().x, canvas_max.y + ImGui::GetWindowPos().y);

        // Рендеринг
        ImGuiDrawList imgui_dl;
        imgui_dl.dl = ImGui::GetWindowDrawList();

        // Сетка
        render_grid(&imgui_dl, app.viewport, canvas_min_pt, canvas_max_pt);

        // Get mouse position for tooltip detection
        Pt hover_world_pos;
        bool has_hover = false;
        if (ImGui::IsWindowHovered()) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            Pt mouse(mouse_pos.x, mouse_pos.y);
            hover_world_pos = app.viewport.screen_to_world(mouse, canvas_min_pt);
            has_hover = true;
        }

        // Blueprint - с симуляцией для подсветки проводов
        TooltipInfo tooltip;
        render_blueprint(app.blueprint, &imgui_dl, app.viewport, canvas_min_pt, canvas_max_pt,
                         &app.interaction.selected_nodes, app.interaction.selected_wire,
                         &app.simulation,
                         has_hover ? &hover_world_pos : nullptr, &tooltip);

        // Render tooltip if active
        render_tooltip(&imgui_dl, tooltip);

        // Render node content (ImGui widgets) - after DrawList rendering
        for (auto& node : app.blueprint.nodes) {
            // Get or create visual node from cache
            auto* visual = app.visual_cache.getOrCreate(node);

            // Update visual node position from current node position (for drag)
            visual->setPosition(node.pos);
            visual->setSize(node.size);

            // Calculate screen position of node
            Pt screen_min = app.viewport.world_to_screen(visual->getPosition(), canvas_min_pt);
            float zoom = app.viewport.zoom;

            // If node has content, use the visual node's layout to get content bounds
            NodeContentType ctype = visual->getContentType();
            if (ctype != NodeContentType::None) {
                Bounds cb = visual->getContentBounds();
                float content_x = screen_min.x + cb.x * zoom;
                float content_y = screen_min.y + cb.y * zoom;
                float available_width = cb.w * zoom;

                if (available_width > 20.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(content_x, content_y));

                    // Get reference to node content for modification
                    NodeContent& content = node.node_content;

                    switch (content.type) {
                        case NodeContentType::Switch: {
                            if (node.type_name == "HoldButton") {
                                // Hold-to-operate button with press/release detection
                                std::string label = content.state ? "PRESSED" : "RELEASED";
                                std::string button_id = label + "##" + node.id;

                                if (ImGui::Button(button_id.c_str(), ImVec2(available_width, 0))) {
                                    app.hold_button_press(node.id);
                                }
                                // Detect when button is released (mouse up while active)
                                if (ImGui::IsItemActive() && ImGui::IsMouseReleased(0)) {
                                    app.hold_button_release(node.id);
                                }
                            } else {
                                // Regular toggle switch
                                std::string label = content.state ? "ON" : "OFF";
                                std::string button_id = label + "##" + node.id;

                                if (ImGui::Button(button_id.c_str(), ImVec2(available_width, 0))) {
                                    app.trigger_switch(node.id);
                                }
                            }
                            break;
                        }
                        case NodeContentType::Value: {
                            ImGui::SetNextItemWidth(available_width);
                            std::string slider_id = "##" + node.id;
                            ImGui::SliderFloat(slider_id.c_str(), &content.value,
                                              content.min, content.max, "%.2f");
                            break;
                        }
                        case NodeContentType::Gauge: {
                            ImGui::SetNextItemWidth(available_width);
                            float progress = (content.value - content.min) / (content.max - content.min);
                            progress = std::max(0.0f, std::min(1.0f, progress));
                            ImGui::ProgressBar(progress);
                            break;
                        }
                        case NodeContentType::Text: {
                            ImGui::Text("%s", content.label.c_str());
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

        // Marquee selection rectangle
        if (app.interaction.marquee_selecting) {
            Pt start_screen = app.viewport.world_to_screen(app.interaction.marquee_start, canvas_min_pt);
            Pt end_screen = app.viewport.world_to_screen(app.interaction.marquee_end, canvas_min_pt);
            Pt min(std::min(start_screen.x, end_screen.x), std::min(start_screen.y, end_screen.y));
            Pt max(std::max(start_screen.x, end_screen.x), std::max(start_screen.y, end_screen.y));
            // Transparent fill + border
            imgui_dl.add_rect_filled(min, max, 0x4000FF00); // semi-transparent green
            imgui_dl.add_rect(min, max, 0xFF00FF00, 1.0f); // green border
        }

        // Обработка мыши через ImGui (когда канва под мышью)
        if (ImGui::IsWindowHovered()) {
            // Zoom через колесико
            if (io.MouseWheel != 0.0f) {
                ImVec2 mouse_pos = ImGui::GetMousePos();
                Pt mouse_screen(mouse_pos.x - canvas_min_pt.x, mouse_pos.y - canvas_min_pt.y);
                app.on_scroll(io.MouseWheel * 10.0f, mouse_screen, canvas_min_pt);
            }

            // Mouse down
            bool alt_down = io.KeyAlt; // Alt = marquee
            bool ctrl_down = io.KeyCtrl || io.KeySuper; // Ctrl/Cmd = add to selection

            if (ImGui::IsMouseClicked(0)) { // Left
                ImVec2 mouse_pos = ImGui::GetMousePos();
                Pt mouse(mouse_pos.x, mouse_pos.y);
                Pt world = app.viewport.screen_to_world(mouse, canvas_min_pt);

                if (alt_down) {
                    // Marquee selection
                    app.interaction.marquee_selecting = true;
                    app.interaction.marquee_start = world;
                    app.interaction.marquee_end = world;
                } else {
                    // Передаем ctrl_down чтобы решить add или replace
                    app.on_mouse_down(world, MouseButton::Left, canvas_min_pt, ctrl_down);
                }
            }
            if (ImGui::IsMouseClicked(1)) { // Right
                ImVec2 mouse_pos = ImGui::GetMousePos();
                Pt mouse(mouse_pos.x, mouse_pos.y);
                Pt world = app.viewport.screen_to_world(mouse, canvas_min_pt);
                app.on_mouse_down(world, MouseButton::Right, canvas_min_pt);
            }
            if (ImGui::IsMouseClicked(2)) { // Middle
                ImVec2 mouse_pos = ImGui::GetMousePos();
                Pt mouse(mouse_pos.x, mouse_pos.y);
                Pt world = app.viewport.screen_to_world(mouse, canvas_min_pt);
                app.on_mouse_down(world, MouseButton::Middle, canvas_min_pt);
            }

            // Double click - добавить/удалить routing point
            if (ImGui::IsMouseDoubleClicked(0)) { // Left double click
                ImVec2 mouse_pos = ImGui::GetMousePos();
                Pt mouse(mouse_pos.x, mouse_pos.y);
                Pt world = app.viewport.screen_to_world(mouse, canvas_min_pt);
                app.on_double_click(world);
            }

            // Mouse drag / panning
            if (ImGui::IsMouseDragging(2)) { // Middle button = panning
                ImVec2 delta = ImGui::GetMouseDragDelta(2);
                Pt world_delta(delta.x / app.viewport.zoom, delta.y / app.viewport.zoom);
                app.on_mouse_drag(world_delta, canvas_min_pt);
                ImGui::ResetMouseDragDelta(2);
            }
            if (ImGui::IsMouseDragging(0)) { // Left button = node drag or marquee
                ImVec2 delta = ImGui::GetMouseDragDelta(0);
                Pt world_delta(delta.x / app.viewport.zoom, delta.y / app.viewport.zoom);

                if (app.interaction.marquee_selecting) {
                    // Marquee selection drag
                    ImVec2 mouse_pos = ImGui::GetMousePos();
                    Pt mouse(mouse_pos.x, mouse_pos.y);
                    Pt world = app.viewport.screen_to_world(mouse, canvas_min_pt);
                    app.interaction.marquee_end = world;
                } else {
                    app.on_mouse_drag(world_delta, canvas_min_pt);
                }
                ImGui::ResetMouseDragDelta(0);
            }

            // Mouse up
            if (ImGui::IsMouseReleased(0)) {
                app.on_mouse_up(MouseButton::Left);
            }
            if (ImGui::IsMouseReleased(1)) {
                app.on_mouse_up(MouseButton::Right);
            }
            if (ImGui::IsMouseReleased(2)) {
                app.on_mouse_up(MouseButton::Middle);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // Context menu for adding components (right-click on empty space)
        if (app.show_context_menu) {
            ImGui::OpenPopup("AddComponent");
            app.show_context_menu = false; // Reset flag
        }

        if (ImGui::BeginPopup("AddComponent")) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Add Component");
            ImGui::Separator();

            // Get sorted list of component classnames
            auto classnames = app.component_registry.list_classnames();
            std::sort(classnames.begin(), classnames.end());

            // Show menu items for each component
            for (const auto& classname : classnames) {
                const auto* def = app.component_registry.get(classname);
                if (def && ImGui::MenuItem(classname.c_str())) {
                    // Add component to blueprint at context_menu_pos
                    app.add_component(classname, app.context_menu_pos);
                }
            }

            ImGui::EndPopup();
        }

        // Рендер
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.118f, 0.118f, 0.137f, 1.0f);  // RGB: 30, 30, 35
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
