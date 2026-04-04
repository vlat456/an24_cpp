#pragma once

namespace visual {

/// Lightweight context passed down during layout so that edge-anchored
/// widgets (e.g. PortRow) can position children relative to the node
/// boundary rather than their immediate parent.
struct LayoutContext {
    float node_width  = 0.0f;
    float node_height = 0.0f;
};

} // namespace visual
