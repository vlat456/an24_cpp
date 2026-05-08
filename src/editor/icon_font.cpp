#include "editor/icon_font.h"
#include <imgui.h>

namespace editor {

ui::IDrawList::NativeFont IconFontLoader::load(ImFontAtlas* atlas, const char* ttf_path, float size_pixels) {
    if (!atlas || !ttf_path) return 0;

    // Minimal glyph range: only the codepoints we actually use.
    static const ImWchar icon_ranges[] = {
        Codepoint::kComposite, Codepoint::kComposite,
        Codepoint::kActive,    Codepoint::kActive,
        Codepoint::kLocked,    Codepoint::kLocked,
        Codepoint::kWarning,   Codepoint::kWarning,
        Codepoint::kError,     Codepoint::kError,
        0
    };

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH  = true;
    config.MergeMode   = false;   // SEPARATE font — do NOT merge into Roboto

    ImFont const* font = atlas->AddFontFromFileTTF(ttf_path, size_pixels, &config, icon_ranges);
    if (font) {
        spdlog::info("Loaded FontAwesome icon font from: {}", ttf_path);
    } else {
        spdlog::warn("Failed to load FontAwesome icon font from: {}", ttf_path);
    }
    return reinterpret_cast<ui::IDrawList::NativeFont>(font);
}

} // namespace editor