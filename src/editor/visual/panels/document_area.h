#pragma once

#include "editor/visual/canvas_renderer.h"
#include "editor/visual/tabs/document_tabs.h"
#include "editor/window_system.h"
#include "editor/data/pt.h"
#include <memory>

struct ImDrawList;


/// Document area with tabs and canvas rendering.
class DocumentArea {
public:
    DocumentArea();
    
    struct Result {
        Document* close_requested = nullptr;
    };
    
    Result render(::WindowSystem& ws, float x, float y, float width, float height);
    float tabBarHeight() const { return tabs_.tabBarHeight(); }

private:
    DocumentTabs tabs_;
    CanvasRenderer canvas_renderer_;
    
    void renderCanvas(::WindowSystem& ws, float x, float y, float width, float height);
};

