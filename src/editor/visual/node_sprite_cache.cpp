/// Node sprite cache implementation.
/// Editor-only — the header is guarded by AN24_EDITOR.

#ifdef AN24_EDITOR

#include "node_sprite_cache.h"
#include "visual/node/visual_node.h"
#include "visual/render_context.h"

#include <imgui.h>
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

void NodeSpriteCache::bake(const NodeWidget& node, const RenderContext& ctx) {
    // Save GL state up-front — ensure_fbo/ensure_texture mutate bindings.
    const GLState gl = save_gl_state();

    const std::string_view nid = node.id();

    // Check if we need to re-bake based on zoom change.
    auto it = cache_.find(nid);
    if (it != cache_.end()) {
        Entry& entry = it->second;
        if (!entry.dirty) {
            // Already clean — check if zoom drifted too far from baked zoom.
            float ratio = std::abs(ctx.zoom - entry.baked_zoom)
                        / std::max(entry.baked_zoom, 0.01f);
            if (ratio < kZoomThreshold) {
                restore_gl_state(gl);
                return;
            }
            entry.dirty = true;
        }
    }

    // Compute texture size in pixels (world-space size × retina scale).
    const Pt node_sz = node.size();
    const int tex_w = std::max(1, static_cast<int>(std::ceil(node_sz.x * kPixelScale)));
    const int tex_h = std::max(1, static_cast<int>(std::ceil(node_sz.y * kPixelScale)));

    // Safety guard against degenerate sizes.
    if (tex_w > 4096 || tex_h > 4096) {
        restore_gl_state(gl);
        return;
    }

    // Ensure shared FBO exists.
    if (!ensure_fbo()) {
        restore_gl_state(gl);
        return;
    }

    // Get or create cache entry.
    if (it == cache_.end()) {
        it = cache_.emplace(nid, Entry{}).first;
    }
    Entry& entry = it->second;

    // Allocate or resize the texture.
    if (!ensure_texture(entry, tex_w, tex_h)) {
        restore_gl_state(gl);
        return;
    }

    // -- Bake: render node primitives into FBO texture --
    // Full implementation in #409 (bake pipeline).
    // For now, just mark as baked so the infrastructure compiles.

    entry.dirty = false;
    entry.baked_zoom = ctx.zoom;

    restore_gl_state(gl);
}

bool NodeSpriteCache::blit(const NodeWidget& node, ImDrawList* dl,
                           const RenderContext& ctx) const {
    auto it = cache_.find(node.id());
    if (it == cache_.end() || !it->second.texture) return false;

    const Entry& entry = it->second;

    // Compute screen-space blit rect from node world bounds.
    const Pt node_pos = node.worldPos();
    const Pt screen_min = ctx.world_to_screen(node_pos);
    const Pt screen_max = ctx.world_to_screen(node_pos + node.size());

    const ImTextureID tex_id = reinterpret_cast<ImTextureID>(
        static_cast<intptr_t>(entry.texture));

    dl->AddImage(tex_id,
                 ImVec2(screen_min.x, screen_min.y),
                 ImVec2(screen_max.x, screen_max.y),
                 ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                 IM_COL32_WHITE);
    return true;
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
