#pragma once

/// Pre-rendered white circle texture for port rendering.
/// Eliminates AddCircleFilled path overhead — uses AddImage (4 flat vertices) instead.
/// Editor-only — compiled in EDITOR_IMGUI_SHELL_SOURCES.

#include "ui/renderer/idraw_list.h"
#include <cmath>
#include <cstring>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace visual {

class PortCircleAtlas {
public:
    static constexpr int kTexSize = 32;

    ~PortCircleAtlas() {
        if (texture_) glDeleteTextures(1, &texture_);
    }

    /// Lazily create the circle texture. Call each frame — no-op after first.
    void ensure() {
        if (texture_) return;

        uint8_t pixels[kTexSize * kTexSize * 4];
        rasterize_circle(pixels);

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTexSize, kTexSize, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /// Opaque native texture handle, safe to store in RenderContext.
    ui::IDrawList::NativeTexture texture_id() const {
        return static_cast<ui::IDrawList::NativeTexture>(texture_);
    }

private:
    GLuint texture_ = 0;

    /// Rasterize a white filled circle with anti-aliased edge into RGBA pixels.
    static void rasterize_circle(uint8_t* out) {
        const float center = kTexSize * 0.5f;
        const float radius = center - 1.0f;
        for (int y = 0; y < kTexSize; y++) {
            for (int x = 0; x < kTexSize; x++) {
                float dx = static_cast<float>(x) - center;
                float dy = static_cast<float>(y) - center;
                float dist = std::sqrt(dx * dx + dy * dy);
                float alpha = 1.0f - smoothstep(radius - 1.0f, radius + 1.0f, dist);
                auto a = static_cast<uint8_t>(alpha * 255.0f);
                int i = (y * kTexSize + x) * 4;
                out[i + 0] = 255; // R
                out[i + 1] = 255; // G
                out[i + 2] = 255; // B
                out[i + 3] = a;   // A
            }
        }
    }

    static float smoothstep(float edge0, float edge1, float x) {
        float t = (x - edge0) / (edge1 - edge0);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
};

} // namespace visual
