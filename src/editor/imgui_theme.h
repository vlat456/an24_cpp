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

/// Load FontAwesome icon font (solid variant) for node badges.
/// Loads only the minimal glyph range needed for badge icons.
/// Must be called BEFORE ImFontAtlas::Build (before first ImGui::NewFrame).
/// Returns the loaded font or nullptr if loading failed.
ImFont* LoadFontAwesome(ImFontAtlas* atlas, float size_pixels = 14.0f);

/// Apply modern dark theme to ImGui
/// Call this after ImGui::CreateContext() but before ImGui::Render()
void ApplyModernDarkTheme();

/// Apply modern light theme to ImGui (alternative)
void ApplyModernLightTheme();

} // namespace ImGuiTheme
