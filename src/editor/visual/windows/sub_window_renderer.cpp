#include "sub_window_renderer.h"
#include "editor/document.h"
#include "editor/window_system.h"
#include <imgui.h>
#include <algorithm>

namespace {

/// Compute bounding box of all nodes in a blueprint and fit the viewport.
/// Shared between root canvas and sub-window renderers.
void fit_viewport_to_blueprint(BlueprintWindow& win, const bp2::Blueprint& bp) {
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

} // namespace


void SubWindowRenderer::renderAll(::WindowSystem& ws) {
    for (auto& doc : ws.documents()) {
        doc->windowManager().remove_closed_windows();
        for (auto& win_ptr : doc->windowManager().windows()) {
            auto& win = *win_ptr;
            // Show sub-windows: either embedded groups (non-root scope)
            // or external-reference windows
            if (!win.is_external_ref() && win.resolved_scope_id().is_root()) continue;
            if (!win.open) continue;
            renderWindow(*doc, win, ws);
        }
    }
}

void SubWindowRenderer::renderWindow(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    
    std::string win_title = win.title;
    if (win.read_only) win_title += " [Read Only]";
    // Include mode prefix in ImGui hash to prevent ID collision between
    // embedded and external windows that share the same scope key string.
    const char* mode_prefix = win.is_external_ref() ? "ext:" : "emb:";
    const std::string win_hash_key = editor::instance_path_to_scope_string(doc.interner(), win.resolved_scope_id().path());
    win_title += " [" + doc.displayName() + "]###" + doc.id().str() + ":" + mode_prefix + win_hash_key;
    
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
        win.input.cancel_gesture();
        doc.autoLayoutEmbedded(win.resolved_scope_id());
        win.pending_auto_fit = true;
    }
    
    ImGui::SameLine();
    
    bool has_sel = !win.input.selected_node_ids().empty();
    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        auto action = doc.applyInputResult(win.input.on_key(Key::Delete), win.resolved_scope_id());
        ws.handleInputAction(action, doc);
    }
    if (!has_sel) ImGui::EndDisabled();
    
    if (win.read_only) ImGui::EndDisabled();
}

void SubWindowRenderer::renderCanvas(Document& doc, BlueprintWindow& win, ::WindowSystem& ws) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    const char* mode_prefix = win.is_external_ref() ? "ext:" : "emb:";
    const std::string canvas_key = editor::instance_path_to_scope_string(doc.interner(), win.resolved_scope_id().path());
    ImGui::InvisibleButton(("##canvas_" + doc.id().str() + "_" + mode_prefix + canvas_key).c_str(), content_size);
    bool hovered = ImGui::IsItemHovered();
    
    auto cmin_region = ImGui::GetWindowContentRegionMin();
    auto cmax_region = ImGui::GetWindowContentRegionMax();
    Pt cmin(cmin_region.x + ImGui::GetWindowPos().x, cmin_region.y + ImGui::GetWindowPos().y);
    Pt cmax(cmax_region.x + ImGui::GetWindowPos().x, cmax_region.y + ImGui::GetWindowPos().y);
    
    canvas_renderer_.render(win, doc, ws, cmin, cmax, ImGui::GetWindowDrawList(), hovered);
}

void SubWindowRenderer::fitViewToContent(Document& doc, BlueprintWindow& win) {
    fit_viewport_to_blueprint(win, win.rendered_blueprint());
}
