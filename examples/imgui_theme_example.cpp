// Example: How to use ImGuiTheme in your application
//
// This example shows how to integrate Roboto font and modern dark theme
// into your ImGui application.

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL2/SDL.h>
#include <GL/gl3w.h>
#include "editor/imgui_theme.h"

int main(int, char**) {
    // === 1. Initialize SDL and OpenGL ===
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("AN-24 Editor with Modern Theme", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // === 2. Initialize ImGui ===
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // === 3. Initialize ImGui Platform/Renderer Backends ===
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // === 4. LOAD ROBOTO FONT & APPLY THEME ===
    // Option A: Load Roboto only (English)
    // ImGuiTheme::LoadRoboto(18.0f);

    // Option B: Load Roboto with Cyrillic support (recommended)
    ImGuiTheme::LoadRobotoWithCyrillic(18.0f);

    // Option C: Apply modern dark theme
    ImGuiTheme::ApplyModernDarkTheme();

    // Option D: Apply modern light theme (alternative)
    // ImGuiTheme::ApplyModernLightTheme();

    // === 5. Main Loop ===
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        // === Create your UI here ===
        ImGui::ShowDemoWindow();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.09f, 0.09f, 0.10f, 1.00f);  // Match dark theme
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // === 6. Cleanup ===
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

/*
=== USAGE IN UNIT TESTS ===

If you're using ImGui in unit tests (like test_render.cpp), you can apply
the theme in your test fixture:

class RenderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGuiTheme::LoadRobotoWithCyrillic(16.0f);
        ImGuiTheme::ApplyModernDarkTheme();
    }

    void TearDown() override {
        ImGui::DestroyContext();
    }
};

=== USAGE IN EXISTING EDITOR APP ===

To integrate into your existing EditorApp, add these lines to your
application initialization:

// After ImGui::CreateContext():
ImGuiTheme::LoadRobotoWithCyrillic(18.0f);
ImGuiTheme::ApplyModernDarkTheme();

That's it! The theme will be applied globally to all ImGui windows.

=== CUSTOMIZATION ===

You can adjust font size, rounding, colors, etc. in imgui_theme.cpp:

// Load different size
ImGuiTheme::LoadRobotoWithCyrillic(20.0f);  // Larger for high-DPI

// Adjust rounding after theme applied
ImGui::GetStyle().WindowRounding = 12.0f;  // More rounded

// Adjust specific color
ImGui::GetStyle().Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);

=== FONT FILES ===

Make sure you have Roboto font files in:
  /Users/vladimir/an24_cpp/src/editor/fonts/

Required files:
  - Roboto-Medium.ttf

You can download Roboto from:
  https://fonts.google.com/specimen/Roboto

Click "Download family" and extract .ttf files to the fonts directory.
*/
