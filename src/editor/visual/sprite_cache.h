/// Abstract sprite cache interface — zero GL/ImGui dependencies.
/// Editor builds use NodeSpriteCache (GL-backed). Test builds pass nullptr.
#pragma once

#include <string_view>

namespace ui { class IDrawList; }
namespace visual {

class Widget;
struct RenderContext;

/// Abstract interface for baking and blitting node sprites.
/// Decouples shared rendering code from the concrete GL implementation.
class ISpriteCache {
public:
    virtual ~ISpriteCache() = default;

    /// Check if a node has a valid cached texture.
    virtual bool has(std::string_view node_id) const = 0;

    /// Blit a cached widget texture. Returns false if not cached.
    virtual bool blit(const Widget& widget, ui::IDrawList* dl,
                      const RenderContext& ctx) const = 0;

    /// Bake all dirty nodes in the scene. Call before Scene::render().
    virtual void bake_dirty_nodes(const class Scene& scene,
                                  const RenderContext& ctx) = 0;

    /// Mark a specific node as needing re-bake.
    virtual void mark_dirty(std::string_view node_id) = 0;

    /// Mark all cached nodes dirty (e.g. zoom changed).
    virtual void mark_all_dirty() = 0;

    /// Free a node's texture (call on node deletion).
    virtual void evict(std::string_view node_id) = 0;

    /// Free all textures and reset.
    virtual void clear() = 0;
};

} // namespace visual
