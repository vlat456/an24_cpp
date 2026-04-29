/// Node sprite cache implementation.
/// Editor-only — the header is guarded by AN24_EDITOR.

#ifdef AN24_EDITOR

#include "node_sprite_cache.h"
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/scene.h"
#include "editor/imgui_draw_list.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction / Destruction
// ============================================================================

NodeSpriteCache::~NodeSpriteCache() {
    clear();
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

// ============================================================================
// Public API
// ============================================================================

bool NodeSpriteCache::has(std::string_view node_id) const {
    auto it = cache_.find(node_id);
    return it != cache_.end() && it->second.texture != 0;
}

void NodeSpriteCache::mark_dirty(std::string_view node_id) {
    auto it = cache_.find(node_id);
    if (it != cache_.end()) {
        it->second.dirty = true;
    }
}

void NodeSpriteCache::mark_all_dirty() {
    for (auto& [id, entry] : cache_) {
        entry.dirty = true;
    }
}

void NodeSpriteCache::evict(std::string_view node_id) {
    auto it = cache_.find(node_id);
    if (it != cache_.end()) {
        if (it->second.texture) {
            glDeleteTextures(1, &it->second.texture);
        }
        cache_.erase(it);
    }
}

void NodeSpriteCache::clear() {
    for (auto& [id, entry] : cache_) {
        if (entry.texture) {
            glDeleteTextures(1, &entry.texture);
        }
    }
    cache_.clear();
}

void NodeSpriteCache::bake(const Widget& widget, const RenderContext& ctx) {
    const std::string_view nid = widget.id();

    // Check if we need to re-bake based on zoom change.
    auto it = cache_.find(nid);
    if (it != cache_.end()) {
        Entry& entry = it->second;
        if (!entry.dirty) {
            float ratio = std::abs(ctx.zoom - entry.baked_zoom)
                        / std::max(entry.baked_zoom, 0.01f);
            if (ratio < kZoomThreshold) return;
            entry.dirty = true;
        }
    }

    // Compute texture size in pixels (world-space size × retina scale).
    const Pt widget_sz = widget.size();
    const int tex_w = std::max(1, static_cast<int>(std::ceil(widget_sz.x * kPixelScale)));
    const int tex_h = std::max(1, static_cast<int>(std::ceil(widget_sz.y * kPixelScale)));

    if (tex_w > 4096 || tex_h > 4096) return;

    // Save GL state for early-return paths (ensure_fbo/ensure_texture mutate bindings).
    // On the success path, ImGui's RenderDrawData restores state internally.
    const GLState gl = save_gl_state();

    if (!ensure_fbo()) { restore_gl_state(gl); return; }

    if (it == cache_.end()) {
        it = cache_.emplace(nid, Entry{}).first;
    }
    Entry& entry = it->second;

    if (!ensure_texture(entry, tex_w, tex_h)) { restore_gl_state(gl); return; }

    // -- Step 1: Render node into a temp ImDrawList --

    ImDrawList temp_dl(ImGui::GetDrawListSharedData());
    // Initialize the draw list — pushes a seed command into CmdBuffer.
    temp_dl._ResetForNewFrame();
    temp_dl.PushClipRectFullScreen();
    temp_dl.PushTextureID(ImGui::GetFont()->ContainerAtlas->TexID);

    // Wrap in our IDrawList adapter so renderTree() can call both
    // IDrawList methods and native ImDrawList* directly (port circles).
    ImGuiDrawList bake_dl;
    bake_dl.dl = &temp_dl;

    // Build a bake-time RenderContext that maps node-local coordinates
    // to texture pixel coordinates:
    //   world_to_screen(P) = (P - pan) * zoom + canvas_min
    // With pan = node.worldPos(), zoom = kPixelScale, canvas_min = (0,0):
    //   world_to_screen(widgetPos + offset) = offset * kPixelScale ✓
    RenderContext bake_ctx;
    bake_ctx.zoom = static_cast<float>(kPixelScale);
    bake_ctx.pan = widget.worldPos();
    bake_ctx.canvas_min = Pt(0, 0);
    bake_ctx.port_circle_texture = ctx.port_circle_texture;
    // Selection state is NOT baked — rendered live as overlay.
    bake_ctx.selected_node_ids = nullptr;

    widget.renderTree(&bake_dl, bake_ctx);

    // -- Step 2: Bake temp draw list to FBO texture via ImGui's GL backend --

    ImDrawData draw_data;
    draw_data.Valid = true;
    draw_data.CmdListsCount = 1;
    draw_data.TotalVtxCount = temp_dl.VtxBuffer.Size;
    draw_data.TotalIdxCount = temp_dl.IdxBuffer.Size;
    ImDrawList* cmd_lists_ptr = &temp_dl;
    draw_data.CmdLists.Data = &cmd_lists_ptr;
    draw_data.CmdLists.Size = 1;
    draw_data.CmdLists.Capacity = 1;
    draw_data.DisplayPos = ImVec2(0.0f, 0.0f);
    draw_data.DisplaySize = ImVec2(static_cast<float>(tex_w),
                                    static_cast<float>(tex_h));
    draw_data.FramebufferScale = ImVec2(1.0f, 1.0f);

    // Bind FBO + attach texture, clear to transparent.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, entry.texture, 0);
    glViewport(0, 0, tex_w, tex_h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ImGui's RenderDrawData saves/restores ALL GL state internally.
    ImGui_ImplOpenGL3_RenderDrawData(&draw_data);

    // Prevent ImDrawData destructor from freeing stack-allocated cmd_lists_ptr.
    draw_data.CmdLists.Data = nullptr;
    draw_data.CmdLists.Size = 0;
    draw_data.CmdLists.Capacity = 0;

    // Unbind FBO.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    entry.dirty = false;
    entry.baked_zoom = ctx.zoom;
}

bool NodeSpriteCache::blit(const Widget& widget, ImDrawList* dl,
                           const RenderContext& ctx) const {
    auto it = cache_.find(widget.id());
    if (it == cache_.end() || !it->second.texture) return false;

    const Entry& entry = it->second;

    // Compute screen-space blit rect from widget world bounds.
    const Pt w_pos = widget.worldPos();
    const Pt screen_min = ctx.world_to_screen(w_pos);
    const Pt screen_max = ctx.world_to_screen(w_pos + widget.size());

    const ImTextureID tex_id = reinterpret_cast<ImTextureID>(
        static_cast<intptr_t>(entry.texture));

    dl->AddImage(tex_id,
                 ImVec2(screen_min.x, screen_min.y),
                 ImVec2(screen_max.x, screen_max.y),
                 ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                 IM_COL32_WHITE);
    return true;
}

void NodeSpriteCache::bake_dirty_nodes(const Scene& scene, const RenderContext& ctx) {
    for (const auto& root : scene.roots()) {
        auto* vw = static_cast<Widget*>(root.get());

        if (!vw->isClickable()) continue;

        // Bake all node types (Node, RefNode, BusNode, GroupNode, TextNode).
        auto kind = vw->kind();
        if (kind != ui::WidgetKind::Node && kind != ui::WidgetKind::RefNode &&
            kind != ui::WidgetKind::BusNode && kind != ui::WidgetKind::GroupNode &&
            kind != ui::WidgetKind::TextNode) continue;

        auto* node = static_cast<Widget*>(vw);
        const std::string_view nid = node->id();

        // Only bake if dirty or not yet in cache.
        auto it = cache_.find(nid);
        if (it != cache_.end() && !it->second.dirty) {
            // Check zoom drift.
            float ratio = std::abs(ctx.zoom - it->second.baked_zoom)
                        / std::max(it->second.baked_zoom, 0.01f);
            if (ratio < kZoomThreshold) continue;
        }

        bake(*node, ctx);    }
}

// ============================================================================
// Private — GL resource management
// ============================================================================

bool NodeSpriteCache::ensure_fbo() {
    if (fbo_) return true;
    glGenFramebuffers(1, &fbo_);
    return fbo_ != 0;
}

bool NodeSpriteCache::ensure_texture(Entry& entry, int w, int h) {
    // Reallocate if size changed.
    if (entry.texture && (entry.width != w || entry.height != h)) {
        glDeleteTextures(1, &entry.texture);
        entry.texture = 0;
    }

    if (!entry.texture) {
        glGenTextures(1, &entry.texture);
        if (!entry.texture) return false;

        glBindTexture(GL_TEXTURE_2D, entry.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Note: caller (bake) must save/restore GL state — this mutates bindings.
        glBindTexture(GL_TEXTURE_2D, 0);

        entry.width = w;
        entry.height = h;
    }

    return true;
}

auto NodeSpriteCache::save_gl_state() const -> GLState {
    GLState s;
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.framebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture);
    s.blend = glIsEnabled(GL_BLEND);
    return s;
}

void NodeSpriteCache::restore_gl_state(const GLState& s) const {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s.framebuffer));
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture));
    if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

} // namespace visual

#endif // AN24_EDITOR
