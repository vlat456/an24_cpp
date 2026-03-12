#include "document_area.h"
#include "editor/window_system.h"
#include <imgui.h>


DocumentArea::DocumentArea() = default;

DocumentArea::Result DocumentArea::render(::WindowSystem& ws, float x, float y, 
                                           float width, float height) {
    Result result;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##DocumentArea", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    
    auto tab_result = tabs_.render(ws);
    result.close_requested = tab_result.close_requested;
    
    float tab_height = tabs_.tabBarHeight();
    if (tab_height < ImGui::GetTextLineHeightWithSpacing()) {
        tab_height = ImGui::GetTextLineHeightWithSpacing();
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + tab_height);
    
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    if (ImGui::BeginChild("##CanvasArea", content_size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        renderCanvas(ws, x, y + tab_height, content_size.x, content_size.y);
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();
    
    return result;
}

void DocumentArea::renderCanvas(::WindowSystem& ws, float, float, 
                                 float, float) {
    Document* active_doc = ws.activeDocument();
    if (!active_doc) return;
    
    auto canvas_min = ImGui::GetWindowContentRegionMin();
    auto canvas_max = ImGui::GetWindowContentRegionMax();
    Pt cmin(canvas_min.x + ImGui::GetWindowPos().x,
            canvas_min.y + ImGui::GetWindowPos().y);
    Pt cmax(canvas_max.x + ImGui::GetWindowPos().x,
            canvas_max.y + ImGui::GetWindowPos().y);
    bool hovered = ImGui::IsWindowHovered();
    
    canvas_renderer_.render(active_doc->root(), *active_doc, ws, cmin, cmax,
                            ImGui::GetWindowDrawList(), hovered);
}

