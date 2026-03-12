#pragma once

#include "editor/document.h"
#include "editor/data/pt.h"


/// Main canvas area renderer - handles tab content and canvas rendering
class CanvasArea {
public:
    /// Render the canvas area for the active document
    void render(class WindowSystem& ws, float menu_height, float available_width, float available_height);

private:
    void renderCanvasContent(Document& doc);
};

