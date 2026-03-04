#pragma once

/**
 * OpenGL configuration for macOS and Linux.
 *
 * macOS: Core Profile 3.2 + Forward Compatible + GLSL 150
 * Linux: Core Profile 3.0+ + GLSL 130
 *
 * macOS не поддерживает Compatibility Profile и legacy GLSL (120, 130).
 * Единственный рабочий путь — Core Profile ≥ 3.2 + GLSL 150.
 *
 * Эти константы используются в an24_editor.cpp и проверяются юнит-тестами.
 */

namespace gl_setup {

#ifdef __APPLE__
    // macOS: минимум GL 3.2 Core, GLSL 150, forward compat обязателен
    constexpr int GL_MAJOR           = 3;
    constexpr int GL_MINOR           = 2;
    constexpr bool CORE_PROFILE      = true;
    constexpr bool FORWARD_COMPAT    = true;
    constexpr const char* GLSL_VERSION = "#version 150";
#else
    // Linux / Windows
    constexpr int GL_MAJOR           = 3;
    constexpr int GL_MINOR           = 0;
    constexpr bool CORE_PROFILE      = true;
    constexpr bool FORWARD_COMPAT    = false;
    constexpr const char* GLSL_VERSION = "#version 130";
#endif

    // Frame buffer
    constexpr int DOUBLE_BUFFER     = 1;
    constexpr int DEPTH_SIZE        = 24;
    constexpr int STENCIL_SIZE      = 8;

} // namespace gl_setup
