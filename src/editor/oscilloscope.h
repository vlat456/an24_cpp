#pragma once

#include "ui/math/pt.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Document;

struct OscilloscopeProbe {
    std::string wire_id;
    std::string doc_id;
    std::string signal_key;
    std::string label;
    std::string group_id;
    ui::Pt world_pos;
    uint32_t color = 0;
};

class OscilloscopeModel {
public:
    void toggle_probe(Document& doc,
                      const std::string& group_id,
                      const std::string& wire_id,
                      const ui::Pt* click_world = nullptr);
    void remove_probe(const std::string& wire_id);
    bool has_probe(const std::string& wire_id) const;

    const OscilloscopeProbe* probe(const std::string& wire_id) const;
    const std::unordered_map<std::string, OscilloscopeProbe>& probes() const { return probes_; }

    void on_blueprint_changed(Document& doc);
    void sample(Document& doc, bool simulation_running, float sample_dt_sec);

    size_t max_samples() const { return max_samples_; }
    float sample_period_sec() const { return sample_period_sec_; }

    struct ChannelView {
        const OscilloscopeProbe* probe = nullptr;
        const std::deque<float>* samples = nullptr;
    };
    std::vector<ChannelView> channels() const;
    struct SampleStats {
        bool has_value = false;
        bool has_tu = false;
        float min_v = 0.0f;
        float max_v = 0.0f;
        float last_v = 0.0f;
        float tu_sec = 0.0f;
    };
    static SampleStats compute_stats(const std::deque<float>& samples, float sample_dt_sec) {
        SampleStats s;
        if (samples.empty()) return s;
        s.has_value = true;
        s.min_v = samples.front();
        s.max_v = samples.front();
        s.last_v = samples.back();
        for (float v : samples) {
            s.min_v = std::min(s.min_v, v);
            s.max_v = std::max(s.max_v, v);
        }

        if (sample_dt_sec <= 0.0f || samples.size() < 8) return s;

        const float range = s.max_v - s.min_v;
        const float prom = std::max(1e-4f, range * 0.05f);
        std::vector<size_t> peaks;
        peaks.reserve(samples.size() / 4);
        for (size_t i = 1; i + 1 < samples.size(); ++i) {
            const float a = samples[i - 1];
            const float b = samples[i];
            const float c = samples[i + 1];
            if (!(b > a && b >= c)) continue;
            if ((b - std::max(a, c)) < prom) continue;
            if (!peaks.empty() && (i - peaks.back()) < 3) continue;
            peaks.push_back(i);
        }

        if (peaks.size() < 2) return s;
        const size_t intervals = std::min<size_t>(peaks.size() - 1, 6);
        const size_t start = (peaks.size() - 1) - intervals;
        float sum = 0.0f;
        for (size_t j = start + 1; j < peaks.size(); ++j) {
            sum += static_cast<float>(peaks[j] - peaks[j - 1]) * sample_dt_sec;
        }
        if (intervals > 0) {
            s.has_tu = true;
            s.tu_sec = sum / static_cast<float>(intervals);
        }
        return s;
    }

    const std::deque<float>& ensure_virtual_channel(Document& doc,
                                                    const std::string& signal_key,
                                                    bool simulation_running);
private:
    size_t max_samples_ = 1200;
    float sample_period_sec_ = 0.0f;
    std::unordered_map<std::string, OscilloscopeProbe> probes_;
    std::unordered_map<std::string, std::deque<float>> samples_;
    std::unordered_map<std::string, std::deque<float>> virtual_samples_;

    static uint32_t color_for_index(size_t i);
};
