#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bp2 {

/// Canonical authored per-node custom color (RGBA, 0.0–1.0).
struct NodeColor {
    float r = 0.5f;
    float g = 0.5f;
    float b = 0.5f;
    float a = 1.0f;

    bool operator==(const NodeColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    static constexpr float min_channel = 0.0f;
    static constexpr float max_channel = 1.0f;

    [[nodiscard]] bool is_valid() const {
        return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a)
            && r >= min_channel && r <= max_channel
            && g >= min_channel && g <= max_channel
            && b >= min_channel && b <= max_channel
            && a >= min_channel && a <= max_channel;
    }

    [[nodiscard]] static NodeColor canonicalized(NodeColor color) {
        auto clamp01 = [](float v) -> float {
            // NaN is unordered — all comparisons return false, so std::clamp
            // passes it through unchanged.  Detect and replace with a safe
            // default before clamping.  ±Inf clamps correctly to [0,1].
            if (std::isnan(v)) return min_channel;
            return std::clamp(v, min_channel, max_channel);
        };
        color.r = clamp01(color.r);
        color.g = clamp01(color.g);
        color.b = clamp01(color.b);
        color.a = clamp01(color.a);
        return color;
    }

    /// Convert to ImGui uint32 ABGR format (0xAABBGGRR).
    /// Canonicalizes first to guarantee NaN-safety on all channels.
    uint32_t to_uint32() const {
        const NodeColor c = canonicalized(*this);
        auto to_u8 = [](float v) -> uint8_t {
            return static_cast<uint8_t>(v * 255.0f + 0.5f);
        };
        return (uint32_t(to_u8(c.a)) << 24) | (uint32_t(to_u8(c.b)) << 16)
             | (uint32_t(to_u8(c.g)) << 8) | uint32_t(to_u8(c.r));
    }
};

} // namespace bp2
