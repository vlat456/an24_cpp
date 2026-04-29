#pragma once

#include "editor/window/blueprint_window.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <imgui.h>
#include <algorithm>

namespace editor {

/// Fit a window's viewport to show all nodes in a blueprint.
/// Computes bounding box of all nodes and calls viewport.fit_content().
inline void fit_viewport_to_blueprint(BlueprintWindow& win, const bp2::Blueprint& bp) {
    Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
    for (const bp2::Blueprint::Node& node : bp.nodes()) {
        bmin.x = std::min(bmin.x, node.layout.x);
        bmin.y = std::min(bmin.y, node.layout.y);
        float w = node.layout.width.value_or(120.0f);
        float h = node.layout.height.value_or(80.0f);
        bmax.x = std::max(bmax.x, node.layout.x + w);
        bmax.y = std::max(bmax.y, node.layout.y + h);
    }
    if (bmin.x < bmax.x && bmin.y < bmax.y) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        win.viewport.fit_content(bmin, bmax, avail.x, avail.y);
    }
}

} // namespace editor
