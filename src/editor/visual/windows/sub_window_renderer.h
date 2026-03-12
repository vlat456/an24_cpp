#pragma once

#include "editor/visual/canvas_renderer.h"
#include "editor/document.h"

class WindowSystem;

namespace an24 {

/// Renders sub-blueprint windows (collapsed groups opened in separate windows).
class SubWindowRenderer {
public:
    void renderAll(WindowSystem& ws);

private:
    CanvasRenderer canvas_renderer_;
    
    void renderWindow(Document& doc, BlueprintWindow& win, WindowSystem& ws);
    void renderToolbar(Document& doc, BlueprintWindow& win, WindowSystem& ws);
    void renderCanvas(Document& doc, BlueprintWindow& win, WindowSystem& ws);
    
    static void fitViewToContent(Document& doc, BlueprintWindow& win);
};

} // namespace an24
