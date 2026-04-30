/// Frame profiler — RAII scoped sections with zero-cost stubs.
///
/// Usage in headers (no #ifdef needed):
///   an24::FrameProfiler profiler_;
///   int prof_events_{};
///   int prof_render_{};
///
/// Usage in implementation:
///   prof_events_ = profiler_.register_section("handleEvents");
///
///   {
///       SCOPED_PROFILE(profiler_, prof_events_);
///       handleEvents();
///   }
///
///   profiler_.maybe_report();
///
/// When AN24_PROFILE is not defined, everything compiles to nothing.

#pragma once

namespace an24 {

#ifdef AN24_PROFILE

struct FrameProfiler {
    static constexpr int MAX_SECTIONS = 32;
    static constexpr int REPORT_INTERVAL = 120;

    const char* names[MAX_SECTIONS]{};
    double accum_us[MAX_SECTIONS]{};
    double total_frame_us = 0.0;
    int frame_count = 0;
    int next_section = 0;

    int register_section(const char* name) {
        int idx = next_section++;
        names[idx] = name;
        return idx;
    }

    void reset() {
        for (int i = 0; i < MAX_SECTIONS; ++i) accum_us[i] = 0.0;
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

#else

/// Stub — all methods are no-ops. Zero cost when AN24_PROFILE is off.
struct FrameProfiler {
    int register_section(const char*) { return 0; }
    void add(int, double) {}
    void add_frame(double) {}
    void maybe_report() {}
};

#endif

/// RAII scoped section. Measures time from construction to destruction.
/// When AN24_PROFILE is off, constructor/destructor are trivial.
struct ScopedSection {
#ifdef AN24_PROFILE
    FrameProfiler& profiler;
    int idx;
    // Separate declaration to avoid including <chrono> in this header.
    // Uses std::chrono::steady_clock internally.
    long long t0_ns;  // epoch nanoseconds, for diff calculation

    ScopedSection(FrameProfiler& p, int i);
    ~ScopedSection();
#else
    ScopedSection(FrameProfiler&, int) {}
#endif
};

} // namespace an24

// === Macro for inline scoped profiling ===

#define AN24_PROF_CONCAT_(a, b) a##b
#define AN24_PROF_CONCAT(a, b) AN24_PROF_CONCAT_(a, b)

/// Place at the start of a scope to profile until scope exit.
/// SCOPED_PROFILE(profiler_, prof_events_)
#define SCOPED_PROFILE(profiler, idx) \
    an24::ScopedSection AN24_PROF_CONCAT(_prof_sec_, __LINE__)(profiler, idx)


// === Inline implementation for ScopedSection (only when profiling) ===

#ifdef AN24_PROFILE
#include <chrono>

inline an24::ScopedSection::ScopedSection(FrameProfiler& p, int i)
    : profiler(p), idx(i),
      t0_ns(std::chrono::steady_clock::now().time_since_epoch().count()) {}

inline an24::ScopedSection::~ScopedSection() {
    auto t1 = std::chrono::steady_clock::now().time_since_epoch();
    auto t0 = std::chrono::steady_clock::duration{t0_ns};
    profiler.add(idx, std::chrono::duration<double, std::micro>(t1 - t0).count());
}

#endif
