#pragma once

#include <cstdint>

namespace editor {

// ============================================================================
// Node badges — small indicator icons in the node title bar
// ============================================================================

/// Badge types that can appear on a node header.
/// Stored as bitmask in NodeBadgeSet — a node can have multiple badges.
enum class NodeBadge : uint8_t {
    Composite = 0,   ///< Node wraps an embedded/referenced blueprint
    Active    = 1,   ///< Simulation actively running on this node
    Locked    = 2,   ///< Node cannot be edited
    Warning   = 3,   ///< Node has validation diagnostics
    Error     = 4,   ///< Node has an error condition
};

/// Bitmask set of NodeBadge values.
/// Trivially copyable (1 byte), no heap, O(1) test.
class NodeBadgeSet {
public:
    constexpr NodeBadgeSet() = default;

    constexpr void set(NodeBadge badge) {
        mask_ |= (1u << static_cast<uint8_t>(badge));
    }
    constexpr void clear(NodeBadge badge) {
        mask_ &= ~(1u << static_cast<uint8_t>(badge));
    }
    constexpr bool test(NodeBadge badge) const {
        return (mask_ & (1u << static_cast<uint8_t>(badge))) != 0;
    }
    constexpr bool empty() const { return mask_ == 0; }
    constexpr uint8_t mask() const { return mask_; }

    /// Iterate: call fn(NodeBadge) for each set badge, in enum order.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (uint8_t i = 0; i < 8; ++i) {
            if (mask_ & (1u << i)) {
                fn(static_cast<NodeBadge>(i));
            }
        }
    }

private:
    uint8_t mask_ = 0;
};

/// Visual properties for a badge: codepoint + ABGR color.
struct BadgeVisuals {
    uint32_t codepoint;   ///< Unicode codepoint (e.g. 0xF126 for fa-code-branch)
    uint32_t color;        ///< Fill color in ImGui ABGR format
};

/// Compile-time lookup: NodeBadge → (codepoint, color).
/// Indexed by static_cast<uint8_t>(badge).
inline BadgeVisuals get_badge_visuals(NodeBadge badge) {
    // Colors in ImGui byte order: 0xAABBGGRR
    static constexpr BadgeVisuals table[] = {
        { 0xF126, 0xFF88AACC },  // Composite — fa-code-branch, teal
        { 0xF0E7, 0xFFCCAA33 },  // Active    — fa-bolt, amber
        { 0xF023, 0xFF999999 },  // Locked    — fa-lock, gray
        { 0xF071, 0xFF66BBCC },  // Warning   — fa-triangle-exclamation, orange
        { 0xF057, 0xFF5577DD },  // Error     — fa-circle-xmark, red
    };
    uint8_t idx = static_cast<uint8_t>(badge);
    return (idx < 5) ? table[idx] : BadgeVisuals{0, 0};
}

// ============================================================================
// Icon font — FontAwesome loader for badge rendering
// ============================================================================

/// FontAwesome icon font handle.
///
/// Stores an opaque font handle (ImFont* cast to void*) so that
/// headers without ImGui dependency can pass it around.
/// Actual loading is done in imgui_theme.cpp.
struct IconFont {
    /// Opaque font handle. Cast from ImFont* in imgui_theme.cpp.
    void* handle = nullptr;

    bool available() const { return handle != nullptr; }

    /// Convert a Unicode codepoint to UTF-8 bytes.
    /// Returns number of bytes written (1-4). Buffer must be >= 5 bytes.
    static int codepoint_to_utf8(uint32_t cp, char out[5]) {
        if (cp > 0x10FFFF) {
            out[0] = '\0';
            return 0;
        }
        if (cp < 0x80) {
            out[0] = static_cast<char>(cp);
            out[1] = '\0';
            return 1;
        } else if (cp < 0x800) {
            out[0] = static_cast<char>(0xC0 | (cp >> 6));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            out[2] = '\0';
            return 2;
        } else if (cp < 0x10000) {
            out[0] = static_cast<char>(0xE0 | (cp >> 12));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            out[3] = '\0';
            return 3;
        } else {
            out[0] = static_cast<char>(0xF0 | (cp >> 18));
            out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (cp & 0x3F));
            out[4] = '\0';
            return 4;
        }
    }
};

} // namespace editor