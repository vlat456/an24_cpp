#pragma once

#include "editor/document.h"
#include "editor/window_system.h"
#include "editor/visual/canvas_renderer.h"


class SubWindowRenderer {
public:
    void renderAll(::WindowSystem& ws);

private:
    CanvasRenderer canvas_renderer_;
    
    void renderWindow(Document& doc, BlueprintWindow& win, ::WindowSystem& ws);
    void renderToolbar(Document& doc, BlueprintWindow& win, ::WindowSystem& ws);
    void renderCanvas(Document& doc, BlueprintWindow& win, ::WindowSystem& ws);
    
    static void fitViewToContent(Document& doc, BlueprintWindow& win);
};

