#include <gtest/gtest.h>

#include "editor/pi_zn_tuner.h"

#include <cmath>

TEST(PIZN, DetectsSustainedOscillationOnSine) {
    std::vector<float> y;
    y.reserve(600);
    constexpr double dt = 1.0 / 60.0;
    constexpr float f = 1.5f;
    for (int i = 0; i < 600; ++i) {
        float t = static_cast<float>(i) * dt;
        y.push_back(std::sin(2.0f * 3.1415926535f * f * t));
    }
    EXPECT_TRUE(zn_is_sustained_oscillation(y, 6));
}

TEST(PIZN, EstimatesTuOnSine) {
    std::vector<float> y;
    y.reserve(600);
    constexpr double dt = 1.0 / 60.0;
    constexpr float f = 2.0f;
    for (int i = 0; i < 600; ++i) {
        float t = static_cast<float>(i) * dt;
        y.push_back(std::sin(2.0f * 3.1415926535f * f * t));
    }
    auto tu = zn_estimate_tu(y, dt);
    ASSERT_TRUE(tu.has_value());
    EXPECT_NEAR(*tu, 0.5f, 0.05f);
}
