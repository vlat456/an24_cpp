#include "imgui_theme.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace ImGuiTheme {

// Helper: Get path to fonts directory
static std::string GetFontPath(const char* font_name) {
    // Try multiple possible locations for fonts directory
    std::vector<std::string> candidates = {
        // Relative to executable (when running from build/)
        std::string("fonts/") + font_name,
        std::string("../src/editor/fonts/") + font_name,
        std::string("../../src/editor/fonts/") + font_name,

        // Relative to source (when running tests)
        std::string("src/editor/fonts/") + font_name,
        std::string("../src/editor/fonts/") + font_name,

        // Absolute path
        std::string("/Users/vladimir/an24_cpp/src/editor/fonts/") + font_name,
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return "";
}

static std::string GetRobotoMediumPath() {
    std::string path = GetFontPath("Roboto-Medium.ttf");
    if (!path.empty()) return path;

    // Fallback to Regular if Medium not found
    spdlog::warn("Roboto-Medium.ttf not found, falling back to Roboto-Regular.ttf");
    return GetFontPath("Roboto-Regular.ttf");
}

ImFont* LoadRoboto(float size_pixels) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();  // Fallback font

    std::string font_path = GetRobotoMediumPath();
    if (font_path.empty()) {
        spdlog::error("Failed to find Roboto font");
        return io.Fonts->Fonts[0];  // Return default font
    }

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 1;
    config.PixelSnapH = false;

    ImFont* roboto = io.Fonts->AddFontFromFileTTF(font_path.c_str(), size_pixels, &config);
    if (roboto) {
        spdlog::info("Loaded Roboto font from: {}", font_path);
        io.FontDefault = roboto;  // Set as default
    } else {
        spdlog::error("Failed to load Roboto font from: {}", font_path);
    }

    return roboto;
}

ImFont* LoadRobotoWithCyrillic(float size_pixels) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();  // Fallback font

    std::string font_path = GetRobotoMediumPath();
    if (font_path.empty()) {
        spdlog::error("Failed to find Roboto font");
        return io.Fonts->Fonts[0];
    }

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 1;
    config.PixelSnapH = false;

    // Load with Cyrillic glyph ranges
    ImFont* roboto = io.Fonts->AddFontFromFileTTF(
        font_path.c_str(),
        size_pixels,
        &config,
        io.Fonts->GetGlyphRangesCyrillic()
    );

    if (roboto) {
        spdlog::info("Loaded Roboto font with Cyrillic support from: {}", font_path);
        io.FontDefault = roboto;
    } else {
        spdlog::error("Failed to load Roboto font from: {}", font_path);
    }

    return roboto;
}

void ApplyModernDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // === BASIC COLORS ===
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);  // Deep dark
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.14f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.22f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // === FRAME BG ===
    colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);

    // === TITLE HEADER ===
    colors[ImGuiCol_TitleBg]                = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    // === MENU BAR ===
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);

    // === SCROLLBAR ===
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);

    // === CHECK MARK ===
    colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);  // Accent blue

    // === SLIDER ===
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);

    // === BUTTON ===
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);  // Accent blue
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);

    // === HEADER (tree node, selectable, etc.) ===
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.29f, 0.36f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.32f, 0.36f, 0.44f, 1.00f);

    // === SEPARATOR ===
    colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);

    // === RESIZE GRIP ===
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);

    // === TAB ===
    colors[ImGuiCol_Tab]                    = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);

    // === PLOT ===
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

    // === TEXT SELECTION ===
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

    // === DRAG DROP ===
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

    // === NAVIGATION ===
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    // === GEOMETRY (ROUNDED CORNERS) ===
    style.WindowRounding     = 8.0f;
    style.FrameRounding      = 5.0f;
    style.PopupRounding      = 6.0f;
    style.GrabRounding       = 5.0f;
    style.TabRounding        = 4.0f;
    style.ScrollbarRounding  = 9.0f;

    // === SPACING ===
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 20.0f;
    style.ColumnsMinSpacing = 6.0f;

    // === SIZE ===
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign   = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.50f);

    // === DISPLAY SAFE AREA ===
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);

    spdlog::info("Applied modern dark theme to ImGui");
}

void ApplyModernLightTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // === BASIC COLORS ===
    colors[ImGuiCol_Text]                   = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);  // Light gray
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.70f, 0.70f, 0.70f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // === FRAME BG ===
    colors[ImGuiCol_FrameBg]                = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);

    // === BUTTON ===
    colors[ImGuiCol_Button]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.60f, 0.70f, 0.95f, 1.00f);  // Light blue
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.50f, 0.60f, 0.85f, 1.00f);

    // === HEADER ===
    colors[ImGuiCol_Header]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    // === GEOMETRY ===
    style.WindowRounding     = 8.0f;
    style.FrameRounding      = 5.0f;
    style.PopupRounding      = 6.0f;
    style.GrabRounding       = 5.0f;
    style.TabRounding        = 4.0f;
    style.ScrollbarRounding  = 9.0f;

    // === SPACING ===
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);

    spdlog::info("Applied modern light theme to ImGui");
}

} // namespace ImGuiTheme
