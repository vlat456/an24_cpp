#pragma once

#include "editor/document.h"
#include "ui/math/pt.h"
#include "editor/window/blueprint_window.h"
#include "editor/window_system.h"
#include "editor/visual/canvas_constants.h"

struct ImDrawList;


/// Renders a single canvas (blueprint window) - handles grid, nodes, wires, input
/// Uses dependency injection for testability
class CanvasRenderer {
public:
    CanvasRenderer() {}
    
    /// Render canvas content and handle input
    void render(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered);

private:
    void renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax, ImDrawList* draw_list);
    void renderBlueprint(BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax, ImDrawList* draw_list);
    void renderTooltips(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin, ImDrawList* draw_list);
    void renderTempWire(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list);
    void renderMarquee(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list);
    void handleInput(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin);

private:
    // Reusable buffer for energized wire set (avoids static thread_local)
    std::unordered_set<std::string_view, visual::StringViewHash> energized_buf_;
};
