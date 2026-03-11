#pragma once

#include "editor/document.h"
#include "editor/data/pt.h"
#include "editor/window/blueprint_window.h"

struct ImDrawList;

namespace an24 {

class WindowSystem;

/// Renders a single canvas (blueprint window) - handles grid, nodes, wires, input
class CanvasRenderer {
public:
    /// Render canvas content and handle input
    void render(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered);

private:
    void renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax);
    void renderBlueprint(BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax);
    void renderTooltips(BlueprintWindow& win, Document& doc, Pt cmin);
    void renderTempWire(BlueprintWindow& win, Pt cmin);
    void renderNodeContent(BlueprintWindow& win, Document& doc, Pt cmin);
    void renderMarquee(BlueprintWindow& win, Pt cmin);
    void handleInput(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin);
};

} // namespace an24
