#include <gtest/gtest.h>

#include "editor/oscilloscope.h"

#include <cmath>

TEST(OscilloscopeStats, SineWaveDetectsTu) {
    std::deque<float> samples;
    constexpr double dt = 1.0 / 60.0;
    constexpr float freq_hz = 2.0f;
    for (int i = 0; i < 180; ++i) {
        const float t = static_cast<float>(i) * dt;
        samples.push_back(std::sin(2.0f * 3.1415926535f * freq_hz * t));
    }

    auto s = OscilloscopeModel::compute_stats(samples, dt);
    ASSERT_TRUE(s.has_value);
    ASSERT_TRUE(s.has_tu);
    EXPECT_NEAR(s.tu_sec, 0.5f, 0.06f);
}

TEST(OscilloscopeStats, FlatTopWaveDetectsTu) {
    std::deque<float> samples;
    constexpr double dt = 1.0 / 60.0;
    constexpr float freq_hz = 2.0f;
    for (int i = 0; i < 180; ++i) {
        const float t = static_cast<float>(i) * dt;
        float v = std::sin(2.0f * 3.1415926535f * freq_hz * t);
        if (v > 0.75f) v = 0.75f;
        samples.push_back(v);
    }

    auto s = OscilloscopeModel::compute_stats(samples, dt);
    ASSERT_TRUE(s.has_value);
    ASSERT_TRUE(s.has_tu);
    EXPECT_NEAR(s.tu_sec, 0.5f, 0.08f);
}

TEST(OscilloscopeStats, DcSignalHasNoTu) {
    std::deque<float> samples(240, 27.5f);
    auto s = OscilloscopeModel::compute_stats(samples, 1.0f / 60.0f);
    ASSERT_TRUE(s.has_value);
    EXPECT_FALSE(s.has_tu);
}
