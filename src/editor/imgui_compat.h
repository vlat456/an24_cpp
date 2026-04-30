/// Thin wrappers around ImGui internal APIs that may change between versions.
/// Centralizes underscore-prefixed calls so only this file needs updating.
#pragma once

#include <imgui.h>

namespace an24 {

/// Reset an ImDrawList for a new frame.
/// Wraps the internal _ResetForNewFrame() which may be renamed/removed.
inline void imgui_draw_list_reset(ImDrawList& dl) {
    dl._ResetForNewFrame();
}

} // namespace an24
