#pragma once

#include "identity.h"
#include "window/window_scope_id.h"
#include "ui/core/interned_id.h"
#include "ui/math/pt.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Document;

struct OscilloscopeProbe {
    std::string probe_id;
    std::string wire_id;
    editor::DocumentId document_id;
    ui::InternedId signal_iid;  ///< Resolved InternedId for zero-lookup sampling. Invalid after sim rebuild.
    std::string label;         ///< Display label (wire_id or resolved key string)
    WindowScopeId scope_id = WindowScopeId::root();
    ui::Pt world_pos;
    uint32_t color = 0;
};

class OscilloscopeModel {
public:
    void toggle_probe(Document& doc,
                      const WindowScopeId& scope_id,
                      const std::string& wire_id,
                      const ui::Pt* click_world = nullptr);
    void remove_probe(const std::string& probe_id);
    bool has_probe(const std::string& probe_id) const;

    /// Remove all probes and samples for a document being closed.
    void purge_for(const editor::DocumentId& doc_id);

    /// Remove all probes, samples, and hover state (for close-all).
    void purge_all();

    const OscilloscopeProbe* probe(const std::string& probe_id) const;
    const std::unordered_map<std::string, OscilloscopeProbe>& probes() const { return probes_; }

    void on_blueprint_changed(Document& doc);
    void sample(Document& doc, bool simulation_running, float sample_dt_sec);
    void set_hover_signal(const editor::DocumentId& doc_id, ui::InternedId signal_iid);
    void clear_hover_signal(const editor::DocumentId& doc_id);
    const std::deque<float>& hover_samples(const editor::DocumentId& doc_id) const;
    ui::InternedId hover_signal_key(const editor::DocumentId& doc_id) const;

    /// Clear all hover state for a document being closed.
    void purge_hover_for(const editor::DocumentId& doc_id);

    size_t max_samples() const { return max_samples_; }
    float sample_period_sec() const { return sample_period_sec_; }

    struct ChannelView {
        const OscilloscopeProbe* probe = nullptr;
        const std::deque<float>* samples = nullptr;
    };
    std::vector<ChannelView> channels_for(const editor::DocumentId& document_id) const;
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

        if (sample_dt_sec <= 0.0f || samples.size() < 6) return s;

        const float range = s.max_v - s.min_v;
        if (range <= 1e-4f) return s;
        const float mid = 0.5f * (s.max_v + s.min_v);
        const float amp_gate = range * 0.20f;
        std::vector<size_t> peaks;
        peaks.reserve(samples.size() / 4);
        for (size_t i = 1; i + 1 < samples.size(); ++i) {
            const float a = samples[i - 1];
            const float b = samples[i];
            const float c = samples[i + 1];
            if (!(b >= a && b > c)) continue;
            if ((b - mid) < amp_gate) continue;
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

private:
    size_t max_samples_ = 1200;
    float sample_period_sec_ = 0.0f;
    std::unordered_map<std::string, OscilloscopeProbe> probes_;
    std::unordered_map<std::string, std::deque<float>> samples_;

    /// Per-document hover state — keyed by DocumentId::str().
    struct HoverState {
        ui::InternedId signal_iid;   ///< Resolved for zero-lookup sampling.
        std::string label;          ///< Display label.
        std::deque<float> samples;
    };
    std::unordered_map<std::string, HoverState> hover_states_;

    static uint32_t color_for_index(size_t i);
};
