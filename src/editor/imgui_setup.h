#pragma once

#include <imgui.h>
#include "imgui_theme.h"

namespace ImGuiSetup {

/// Initialize ImGui with Roboto font and modern dark theme.
/// Call this after ImGui::CreateContext() and before ImGui::Render().
///
/// Usage:
///   ImGui::CreateContext();
///   ImGuiSetup::InitWithRobotoTheme();
///   // ... main loop ...
inline void InitWithRobotoTheme() {
    ImGuiTheme::LoadRobotoWithCyrillic(18.0f);
    ImGuiTheme::ApplyModernDarkTheme();
}

/// Initialize ImGui with custom font size and dark theme.
inline void InitWithRobotoTheme(float font_size) {
    ImGuiTheme::LoadRobotoWithCyrillic(font_size);
    ImGuiTheme::ApplyModernDarkTheme();
}

/// Initialize ImGui with light theme instead of dark.
inline void InitWithRobotoLightTheme() {
    ImGuiTheme::LoadRobotoWithCyrillic(18.0f);
    ImGuiTheme::ApplyModernLightTheme();
}

} // namespace ImGuiSetup
