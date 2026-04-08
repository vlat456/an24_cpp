#include "sub_window_renderer.h"
#include "editor/window_system.h"
#include "editor/input/input_types.h"
#include "editor/visual/scene_mutations.h"
#include <imgui.h>
#include <algorithm>


void SubWindowRenderer::renderAll(::WindowSystem& ws) {
    for (auto& doc : ws.documents()) {
        doc->windowManager().remove_closed_windows();
        for (auto& win_ptr : doc->windowManager().windows()) {
            auto& win = *win_ptr;
            // Show sub-windows: either embedded groups (non-empty scope_id)
            // or external-reference windows
            if (!win.is_external_ref() && win.scope_id.empty()) continue;
            if (!win.open) continue;
            renderWindow(*doc, win, ws);
        }
    }
}

void SubWindowRenderer::renderWindow(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    
    std::string win_title = win.title;
    if (win.read_only) win_title += " [Read Only]";
    // External-ref windows use parent_instance_id for unique ImGui hash
    // (scope_id is empty for external refs, which would cause ID collisions).
    const std::string& win_hash_key = win.is_external_ref()
        ? win.parent_instance_id : win.scope_id;
    win_title += " [" + doc.displayName() + "]###" + doc.id() + ":" + win_hash_key;
    
    if (!ImGui::Begin(win_title.c_str(), &win.open,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    
    renderToolbar(doc, win, ws);
    if (win.pending_auto_fit) {
        fitViewToContent(doc, win);
        win.pending_auto_fit = false;
    }
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
        // TODO Phase 8: implement auto_layout_group as a bp2 command.
        // For now, just cancel any in-flight gesture and rebuild.
        win.input.cancel_gesture();
        const bp2::Blueprint& rebuild_bp = win.rendered_blueprint();
        ui::StringInterner& rebuild_interner = win.rendered_interner();
        bp2::PathArena& rebuild_arena = win.rendered_arena();
        const std::string& rebuild_group = win.is_external_ref() ? "" : win.scope_id;
        visual::mutations::rebuild(win.scene, rebuild_bp,
                                   rebuild_interner, rebuild_arena, rebuild_group);
        fitViewToContent(doc, win);
    }
    
    ImGui::SameLine();
    
    bool has_sel = !win.input.selected_nodes().empty();
    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        auto action = doc.applyInputResult(win.input.on_key(Key::Delete), win.scope_id);
        ws.handleInputAction(action, doc);
    }
    if (!has_sel) ImGui::EndDisabled();
    
    if (win.read_only) ImGui::EndDisabled();
}

void SubWindowRenderer::renderCanvas(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    const std::string& canvas_key = win.is_external_ref()
        ? win.parent_instance_id : win.scope_id;
    ImGui::InvisibleButton(("##canvas_" + doc.id() + "_" + canvas_key).c_str(), content_size);
    bool hovered = ImGui::IsItemHovered();
    
    auto cmin_region = ImGui::GetWindowContentRegionMin();
    auto cmax_region = ImGui::GetWindowContentRegionMax();
    Pt cmin(cmin_region.x + ImGui::GetWindowPos().x, cmin_region.y + ImGui::GetWindowPos().y);
    Pt cmax(cmax_region.x + ImGui::GetWindowPos().x, cmax_region.y + ImGui::GetWindowPos().y);
    
    canvas_renderer_.render(win, doc, ws, cmin, cmax, ImGui::GetWindowDrawList(), hovered);
}

void SubWindowRenderer::fitViewToContent(Document& doc, BlueprintWindow& win) {
    Pt bmin(1e9f, 1e9f), bmax(-1e9f, -1e9f);
    // For external-ref windows, iterate the external blueprint's nodes (root scope)
    const bp2::Blueprint& bp = win.rendered_blueprint();
    for (const bp2::Blueprint::Node& node : bp.nodes()) {
        if (win.scope_id.empty() && !node.layout.layout_group.empty()) continue;
        bmin.x = std::min(bmin.x, node.layout.x);
        bmin.y = std::min(bmin.y, node.layout.y);
        float w = node.layout.width.value_or(120.0f);
        float h = node.layout.height.value_or(80.0f);
        bmax.x = std::max(bmax.x, node.layout.x + w);
        bmax.y = std::max(bmax.y, node.layout.y + h);
    }
    if (bmin.x < bmax.x && bmin.y < bmax.y) {
        ImVec2 ws = ImGui::GetContentRegionAvail();
        win.viewport.fit_content(bmin, bmax, ws.x, ws.y);
    }
}
