#pragma once

/// Platform-independent OpenGL header.
/// Centralizes the macOS / Linux GL include divergence that was previously
/// duplicated across node_sprite_cache.h, port_circle_atlas.h, etc.

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif
