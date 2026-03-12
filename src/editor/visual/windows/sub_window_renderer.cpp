#include "sub_window_renderer.h"
#include "editor/window_system.h"
#include "editor/input/input_types.h"
#include <imgui.h>
#include <algorithm>


void SubWindowRenderer::renderAll(::WindowSystem& ws) {
    for (auto& doc : ws.documents()) {
        doc->windowManager().removeClosedWindows();
        for (auto& win_ptr : doc->windowManager().windows()) {
            auto& win = *win_ptr;
            if (win.group_id.empty() || !win.open) continue;
            renderWindow(*doc, win, ws);
        }
    }
}

void SubWindowRenderer::renderWindow(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    
    std::string win_title = win.title;
    if (win.read_only) win_title += " [Read Only]";
    win_title += " [" + doc.displayName() + "]###" + doc.id() + ":" + win.group_id;
    
    if (!ImGui::Begin(win_title.c_str(), &win.open,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    
    renderToolbar(doc, win, ws);
    renderCanvas(doc, win, ws);
    
    ImGui::End();
}

void SubWindowRenderer::renderToolbar(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    if (ImGui::Button("Fit View")) {
        fitViewToContent(doc, win);
    }
    
    ImGui::SameLine();
    
    if (win.read_only) ImGui::BeginDisabled();
    
    if (ImGui::Button("Auto Layout")) {
        doc.blueprint().auto_layout_group(win.group_id);
        win.scene.clearCache();
        fitViewToContent(doc, win);
    }
    
    ImGui::SameLine();
    
    bool has_sel = !win.input.selected_nodes().empty();
    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        auto action = doc.applyInputResult(win.input.on_key(Key::Delete), win.group_id);
        ws.handleInputAction(action, doc);
    }
    if (!has_sel) ImGui::EndDisabled();
    
    if (win.read_only) ImGui::EndDisabled();
}

void SubWindowRenderer::renderCanvas(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton(("##canvas_" + doc.id() + "_" + win.group_id).c_str(), content_size);
    bool hovered = ImGui::IsItemHovered();
    
    auto cmin_region = ImGui::GetWindowContentRegionMin();
    auto cmax_region = ImGui::GetWindowContentRegionMax();
    Pt cmin(cmin_region.x + ImGui::GetWindowPos().x, cmin_region.y + ImGui::GetWindowPos().y);
    Pt cmax(cmax_region.x + ImGui::GetWindowPos().x, cmax_region.y + ImGui::GetWindowPos().y);
    
    canvas_renderer_.render(win, doc, ws, cmin, cmax, ImGui::GetWindowDrawList(), hovered);
}

void SubWindowRenderer::fitViewToContent(Document& doc, BlueprintWindow& win) {
    Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
    for (const auto& node : doc.blueprint().nodes) {
        if (node.group_id != win.group_id) continue;
        bmin.x = std::min(bmin.x, node.pos.x);
        bmin.y = std::min(bmin.y, node.pos.y);
        bmax.x = std::max(bmax.x, node.pos.x + node.size.x);
        bmax.y = std::max(bmax.y, node.pos.y + node.size.y);
    }
    if (bmin.x < bmax.x && bmin.y < bmax.y) {
        ImVec2 ws = ImGui::GetContentRegionAvail();
        win.scene.viewport().fit_content(bmin, bmax, ws.x, ws.y);
    }
}

