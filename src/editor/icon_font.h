#pragma once

#include "ui/renderer/idraw_list.h"
#include <cstdint>
#include <spdlog/spdlog.h>

struct ImFontAtlas;

namespace editor {

/// FontAwesome icon font loader.
///
/// Loads a separate ImFont (NOT merged into Roboto) so that:
/// - Per-icon color control is possible (separate add_text calls)
/// - Graceful degradation when TTF is missing (available() returns false)
/// - Independent oversample/pixel-snap tuning
///
/// Must be loaded BEFORE ImFontAtlas::Build (i.e. before first ImGui::NewFrame).
struct IconFontLoader {
    /// FontAwesome 6 Free Solid codepoints for node badges.
    struct Codepoint {
        static constexpr uint32_t kComposite = 0xF126;   // fa-code-branch
        static constexpr uint32_t kActive    = 0xF0E7;   // fa-bolt
        static constexpr uint32_t kLocked    = 0xF023;   // fa-lock
        static constexpr uint32_t kWarning   = 0xF071;   // fa-triangle-exclamation
        static constexpr uint32_t kError     = 0xF057;   // fa-circle-xmark
    };

    /// Load FontAwesome from TTF path, adding only the glyphs we need.
    /// Returns an opaque NativeFont handle (ImFont* cast to uintptr_t).
    /// Returns 0 if loading failed.
    /// Must be called BEFORE ImFontAtlas::Build (before first ImGui::NewFrame).
    static ui::IDrawList::NativeFont load(ImFontAtlas* atlas, const char* ttf_path, float size_pixels);
};

} // namespace editor