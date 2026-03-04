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
#include "debug.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL2/SDL.h>

// OpenGL includes
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <cstdio>

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

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }

            // Обработка событий мыши для редактора (когда не в ImGui)
            if (!io.WantCaptureMouse) {
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    Pt mouse(event.button.x, event.button.y);
                    Pt canvas_min(0, 0); // TODO: получить реальный canvas
                    MouseButton btn = (event.button.button == SDL_BUTTON_LEFT) ? MouseButton::Left :
                                     (event.button.button == SDL_BUTTON_MIDDLE) ? MouseButton::Middle : MouseButton::Right;
                    Pt world = app.viewport.screen_to_world(mouse, canvas_min);
                    app.on_mouse_down(world, btn, canvas_min);
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    MouseButton btn = (event.button.button == SDL_BUTTON_LEFT) ? MouseButton::Left :
                                     (event.button.button == SDL_BUTTON_MIDDLE) ? MouseButton::Middle : MouseButton::Right;
                    app.on_mouse_up(btn);
                }
                if (event.type == SDL_MOUSEWHEEL) {
                    Pt mouse(event.wheel.x, event.wheel.y);
                    app.on_scroll((float)event.wheel.y * 0.1f, mouse, Pt(0, 0));
                }
            }

            // Обработка клавиатуры
            if (!io.WantCaptureKeyboard && event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    app.on_key_down(Key::Escape);
                } else if (event.key.keysym.sym == SDLK_DELETE) {
                    app.on_key_down(Key::Delete);
                }
            }
        }

        // ImGui новый кадр
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Меню
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    app.new_circuit();
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    // TODO: file dialog
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    // TODO: save
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

        // Canvas область
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        auto canvas_min = ImGui::GetWindowContentRegionMin();
        auto canvas_max = ImGui::GetWindowContentRegionMax();
        Pt canvas_min_pt(canvas_min.x + ImGui::GetWindowPos().x, canvas_min.y + ImGui::GetWindowPos().y);
        Pt canvas_max_pt(canvas_max.x + ImGui::GetWindowPos().x, canvas_max.y + ImGui::GetWindowPos().y);

        // Рендеринг
        ImGuiDrawList imgui_dl;
        imgui_dl.dl = ImGui::GetWindowDrawList();

        // Сетка
        render_grid(&imgui_dl, app.viewport, canvas_min_pt, canvas_max_pt);

        // Blueprint
        render_blueprint(app.blueprint, &imgui_dl, app.viewport, canvas_min_pt, canvas_max_pt);

        ImGui::End();
        ImGui::PopStyleVar();

        // Рендер
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
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
