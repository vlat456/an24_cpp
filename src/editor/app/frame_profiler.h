#pragma once

#ifdef AN24_PROFILE

#include <array>
#include <chrono>
#include <cstdio>

namespace an24 {

struct FrameProfiler {
    static constexpr int MAX_SECTIONS = 16;
    static constexpr int REPORT_INTERVAL = 120;

    std::array<const char*, MAX_SECTIONS> names{};
    std::array<double, MAX_SECTIONS> accum_us{};
    double total_frame_us = 0.0;
    int frame_count = 0;
    int next_section = 0;

    int register_section(const char* name) {
        int idx = next_section++;
        names[idx] = name;
        return idx;
    }

    void reset() {
        accum_us.fill(0.0);
        total_frame_us = 0.0;
        frame_count = 0;
    }

    void add(int idx, double us) { accum_us[idx] += us; }
    void add_frame(double us) { total_frame_us += us; ++frame_count; }

    void maybe_report() {
        if (frame_count < REPORT_INTERVAL) return;
        double avg = total_frame_us / frame_count;
        double accounted = 0.0;
        std::printf("\n=== Frame Profiler (avg over %d frames, %.1f ms/frame) ===\n",
                    frame_count, avg / 1000.0);
        for (int i = 0; i < next_section; ++i) {
            double sec_avg = accum_us[i] / frame_count;
            double pct = (accum_us[i] / total_frame_us) * 100.0;
            std::printf("  %-45s %8.1f us  (%5.1f%%)\n", names[i], sec_avg, pct);
            accounted += accum_us[i];
        }
        double unaccounted = total_frame_us - accounted;
        double unacc_avg = unaccounted / frame_count;
        std::printf("  %-45s %8.1f us  (%5.1f%%)\n",
                    "unaccounted", unacc_avg,
                    total_frame_us > 0.0 ? (unaccounted / total_frame_us) * 100.0 : 0.0);
        std::fflush(stdout);
        reset();
    }
};

struct ScopedSection {
    FrameProfiler& profiler;
    int idx;
    std::chrono::steady_clock::time_point t0;

    ScopedSection(FrameProfiler& p, int idx)
        : profiler(p), idx(idx), t0(std::chrono::steady_clock::now()) {}

    ~ScopedSection() {
        auto t1 = std::chrono::steady_clock::now();
        profiler.add(idx, std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
};

} // namespace an24

#define AN24_PROFILE_SECTION(profiler, idx) \
    an24::ScopedSection _prof_sec_##idx(profiler, idx)

#endif // AN24_PROFILE
