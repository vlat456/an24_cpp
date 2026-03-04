#include <gtest/gtest.h>
#include "editor/gl_setup.h"
#include <cstring>
#include <string>

/// TDD Step 9: GL setup configuration
///
/// Верифицирует что OpenGL-конфигурация корректна для текущей платформы.
/// Основная проблема: macOS не поддерживает legacy GLSL (120, 130),
/// только Core Profile 3.2+ с GLSL 150.

// ========== macOS-specific tests ==========

#ifdef __APPLE__

TEST(GLSetupTest, MacOS_GLVersion_Is_3_2_Plus) {
    // macOS поддерживает OpenGL 3.2-4.1 в Core Profile
    EXPECT_GE(gl_setup::GL_MAJOR, 3);
    if (gl_setup::GL_MAJOR == 3) {
        EXPECT_GE(gl_setup::GL_MINOR, 2);
    }
}

TEST(GLSetupTest, MacOS_CoreProfile_Required) {
    // macOS не работает с Compatibility Profile
    EXPECT_TRUE(gl_setup::CORE_PROFILE);
}

TEST(GLSetupTest, MacOS_ForwardCompat_Required) {
    // Forward compatibility обязателен для macOS OpenGL Core Profile
    EXPECT_TRUE(gl_setup::FORWARD_COMPAT);
}

TEST(GLSetupTest, MacOS_GLSL_Version_Is_150_Plus) {
    // macOS Core Profile 3.2 → GLSL 150
    // Версии 120, 130 не поддерживаются в Core Profile
    std::string glsl = gl_setup::GLSL_VERSION;
    EXPECT_NE(glsl.find("150"), std::string::npos)
        << "macOS requires GLSL 150, got: " << glsl;
}

TEST(GLSetupTest, MacOS_GLSL_NotLegacy) {
    // Защита от регрессии: убеждаемся что не 120 или 130
    std::string glsl = gl_setup::GLSL_VERSION;
    EXPECT_EQ(glsl.find("120"), std::string::npos)
        << "GLSL 120 breaks on macOS Core Profile";
    EXPECT_EQ(glsl.find("130"), std::string::npos)
        << "GLSL 130 breaks on macOS Core Profile";
}

#endif // __APPLE__

// ========== Platform-independent tests ==========

TEST(GLSetupTest, GLSLVersion_StartsWithVersion) {
    std::string glsl = gl_setup::GLSL_VERSION;
    EXPECT_EQ(glsl.substr(0, 9), "#version ")
        << "GLSL version string must start with '#version '";
}

TEST(GLSetupTest, GLSLVersion_HasNumericVersion) {
    std::string glsl = gl_setup::GLSL_VERSION;
    // Должна быть числовая версия после "#version "
    std::string num = glsl.substr(9);
    EXPECT_FALSE(num.empty());
    int version = std::stoi(num);
    EXPECT_GE(version, 120) << "GLSL version too low: " << version;
    EXPECT_LE(version, 460) << "GLSL version too high: " << version;
}

TEST(GLSetupTest, CoreProfile_GLMajor_AtLeast_3) {
    // Core profile требует минимум GL 3.0
    if (gl_setup::CORE_PROFILE) {
        EXPECT_GE(gl_setup::GL_MAJOR, 3);
    }
}

TEST(GLSetupTest, DepthSize_Is_24) {
    EXPECT_EQ(gl_setup::DEPTH_SIZE, 24);
}

TEST(GLSetupTest, StencilSize_Is_8) {
    EXPECT_EQ(gl_setup::STENCIL_SIZE, 8);
}

TEST(GLSetupTest, DoubleBuffer_Enabled) {
    EXPECT_EQ(gl_setup::DOUBLE_BUFFER, 1);
}

TEST(GLSetupTest, GLSL_Version_Matches_GL_Version) {
    // GL 3.2 → GLSL 150, GL 3.0 → GLSL 130, GL 3.3 → GLSL 330
    std::string glsl = gl_setup::GLSL_VERSION;
    int version = std::stoi(glsl.substr(9));

    if (gl_setup::GL_MAJOR == 3 && gl_setup::GL_MINOR == 2) {
        EXPECT_EQ(version, 150)
            << "GL 3.2 should use GLSL 150, got " << version;
    } else if (gl_setup::GL_MAJOR == 3 && gl_setup::GL_MINOR == 0) {
        EXPECT_EQ(version, 130)
            << "GL 3.0 should use GLSL 130, got " << version;
    } else if (gl_setup::GL_MAJOR == 3 && gl_setup::GL_MINOR == 3) {
        EXPECT_EQ(version, 330)
            << "GL 3.3 should use GLSL 330, got " << version;
    }
}
