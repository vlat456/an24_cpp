#pragma once

/// Node sprite cache — bakes node widgets to GL textures for zero-cost
/// steady-state rendering. Each cached node is blitted with a single
/// AddImage call (4 vertices, ~1us) instead of ~45 ImGui draw calls.
///
/// Editor-only (requires OpenGL + ImGui). Guarded by AN24_EDITOR.

#ifdef AN24_EDITOR

#include "ui/math/pt.h"
#include <unordered_map>
#include <string_view>
#include <cstdint>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

struct ImDrawList;

namespace visual {

class Widget;
struct RenderContext;

/// Manages GL textures for baked node sprites.
///
/// Lifecycle:
/// - Lazily creates a shared FBO on first use.
/// - Allocates one GL_TEXTURE_2D per node, resized as needed.
/// - Textures freed via evict() (node deletion) or destructor.
/// - Caller marks nodes dirty; bake() re-renders dirty entries.
class NodeSpriteCache {
public:
    NodeSpriteCache() = default;
    ~NodeSpriteCache();

    // Non-copyable, non-movable (owns GL resources).
    NodeSpriteCache(const NodeSpriteCache&) = delete;
    NodeSpriteCache& operator=(const NodeSpriteCache&) = delete;
    NodeSpriteCache(NodeSpriteCache&&) = delete;
    NodeSpriteCache& operator=(NodeSpriteCache&&) = delete;

    /// Bake a dirty widget to its texture. No-op if not dirty.
    void bake(const Widget& widget, const RenderContext& ctx);

    /// Blit a cached widget texture via AddImage. Returns false if not cached.
    bool blit(const Widget& widget, ImDrawList* dl, const RenderContext& ctx) const;

    /// Bake all dirty nodes in the scene. Call before Scene::render().
    void bake_dirty_nodes(const class Scene& scene, const RenderContext& ctx);

    /// Mark a specific node as needing re-bake.
    void mark_dirty(std::string_view node_id);

    /// Mark all cached nodes dirty (e.g. zoom changed).
    void mark_all_dirty();

    /// Check if a node has a valid cached texture.
    bool has(std::string_view node_id) const;

    /// Free a node's texture (call on node deletion).
    void evict(std::string_view node_id);

    /// Free all textures and reset.
    void clear();

private:
    /// One cached texture per node, keyed by node id (string_view into
    /// stable StringInterner storage — valid for document lifetime).
    struct Entry {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        bool dirty = true;
        /// Zoom level at which this texture was baked.
        /// Used to decide if re-bake is needed on zoom change.
        float baked_zoom = 0.0f;
    };

    /// Ensure the shared FBO exists. Returns true on success.
    bool ensure_fbo();

    /// Allocate or resize a texture for the given dimensions.
    /// Returns true if texture is valid and matches (w, h).
    bool ensure_texture(Entry& entry, int w, int h);

    /// Save/restore OpenGL state around FBO rendering.
    struct GLState {
        GLint viewport[4] = {};
        GLint framebuffer = 0;
        GLint texture = 0;
        GLboolean blend = GL_FALSE;
    };
    GLState save_gl_state() const;
    void restore_gl_state(const GLState& state) const;

    std::unordered_map<std::string_view, Entry> cache_;
    GLuint fbo_ = 0;

    /// Zoom threshold ratio — re-bake when zoom changes by more than this.
    static constexpr float kZoomThreshold = 0.2f;

    /// Texture pixel scale for retina / quality. 2x = crisp at moderate zoom.
    static constexpr int kPixelScale = 2;
};

} // namespace visual

#endif // AN24_EDITOR
