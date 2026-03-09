#pragma once

#include <imgui.h>
#include <string>
#include <filesystem>

namespace ImGuiTheme {

/// Load Roboto font from the editor/fonts directory
/// Returns the loaded font or nullptr if loading failed
ImFont* LoadRoboto(float size_pixels = 18.0f);

/// Load Roboto with Cyrillic support
ImFont* LoadRobotoWithCyrillic(float size_pixels = 18.0f);

/// Apply modern dark theme to ImGui
/// Call this after ImGui::CreateContext() but before ImGui::Render()
void ApplyModernDarkTheme();

/// Apply modern light theme to ImGui (alternative)
void ApplyModernLightTheme();

} // namespace ImGuiTheme
