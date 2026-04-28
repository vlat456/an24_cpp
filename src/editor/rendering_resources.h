#pragma once

namespace editor {

struct IconFont;  // forward declaration — pointer only

/// App-lifetime rendering infrastructure shared across all documents.
/// Owned by WindowSystem, passed by const pointer through the construction chain.
/// Semantically distinct from RenderContext (per-frame) — this is immutable after init.
struct RenderingResources {
    const IconFont* icon_font = nullptr;
    // Future: const IconFont* icon_font_small;
    // Future: const TextureAtlas* node_icons;
};

} // namespace editor