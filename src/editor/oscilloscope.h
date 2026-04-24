#pragma once

#include "identity.h"
#include "window/window_scope_id.h"
#include "ui/core/interned_id.h"
#include "ui/math/pt.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Document;

// =============================================================================
// WindowScopeId hash — used by ProbeKey
// =============================================================================

struct WindowScopeIdHash {
    size_t operator()(const WindowScopeId& id) const noexcept {
        size_t h = static_cast<size_t>(id.mode());
        for (auto seg : id.path()) {
            h = h * 31 + std::hash<ui::InternedId>{}(seg);
        }
        return h;
    }
};

// =============================================================================
// OscilloscopeProbe
// =============================================================================

/// A single oscilloscope probe, co-located with its sample buffer.
///
/// Identity within a per-document partition: (scope_id, wire_iid).
/// No probe_id string, no document_id field — the partition owns those.
/// scope_id lives in the ProbeKey (map key), not duplicated here.
struct OscilloscopeProbe {
    ui::InternedId wire_iid;       ///< Interned wire identity from the document's interner.
    ui::InternedId signal_iid;     ///< Resolved InternedId for zero-lookup sampling. Invalid after sim rebuild.
    std::string label;             ///< Display label (wire_id or resolved key string).
    ui::Pt world_pos;              ///< Anchor position on the wire for marker rendering.
    uint32_t color = 0;
    std::deque<float> samples;     ///< Co-located sample buffer.
};

// =============================================================================
// ProbeKey — identity for probe lookup within a document partition
// =============================================================================

/// Composite key: a probe is uniquely identified by its scope + wire.
struct ProbeKey {
    WindowScopeId scope_id;
    ui::InternedId wire_iid;

    bool operator==(const ProbeKey& other) const {
        return scope_id == other.scope_id && wire_iid == other.wire_iid;
    }
};

struct ProbeKeyHash {
    size_t operator()(const ProbeKey& k) const noexcept {
        // boost::hash_combine: h ^= std::hash<T>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        size_t h = WindowScopeIdHash{}(k.scope_id);
        h ^= std::hash<ui::InternedId>{}(k.wire_iid) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// =============================================================================
// OscilloscopeModel
// =============================================================================

class OscilloscopeModel {
public:
    /// Toggle a probe on/off for a wire within a scope.
    void toggle_probe(Document& doc,
                      const WindowScopeId& scope_id,
                      ui::InternedId wire_iid,
                      const ui::Pt* click_world = nullptr);

    /// Remove all probes and samples for a document being closed.
    void purge_for(const editor::DocumentId& doc_id);

    /// Remove all probes, samples, and hover state (for close-all).
    void purge_all();

    /// Resolves signal keys and anchors after a blueprint rebuild.
    void on_blueprint_changed(Document& doc);

    /// Sample all probes for a document + hover state.
    void sample(Document& doc, bool simulation_running, float sample_dt_sec);

    // -- Hover state (per-document) --

    void set_hover_signal(const editor::DocumentId& doc_id, ui::InternedId signal_iid);
    void clear_hover_signal(const editor::DocumentId& doc_id);
    const std::deque<float>& hover_samples(const editor::DocumentId& doc_id) const;
    ui::InternedId hover_signal_key(const editor::DocumentId& doc_id) const;

    /// Clear all hover state for a document being closed.
    void purge_hover_for(const editor::DocumentId& doc_id);

    // -- Accessors --

    size_t max_samples() const { return max_samples_; }
    float sample_period_sec() const { return sample_period_sec_; }

    /// View onto a probe + its samples for rendering.
    struct ChannelView {
        const OscilloscopeProbe* probe = nullptr;
    };
    std::vector<ChannelView> channels_for(const editor::DocumentId& document_id) const;

    /// Iterate probes matching (doc_id, scope_id) — used by canvas_renderer
    /// for probe marker rendering.
    void for_each_probe_in_scope(
        const editor::DocumentId& doc_id,
        const WindowScopeId& scope_id,
        const std::function<void(const OscilloscopeProbe&)>& fn) const;

    // -- Stats computation (stateless) --

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

    // -- Per-document probe partition --

    struct DocumentProbes {
        std::unordered_map<ProbeKey, OscilloscopeProbe, ProbeKeyHash> probes;
    };
    std::unordered_map<editor::DocumentId, DocumentProbes> docs_;

    DocumentProbes* find_doc(const editor::DocumentId& doc_id);
    const DocumentProbes* find_doc(const editor::DocumentId& doc_id) const;

    // -- Hover state (per-document) --

    struct HoverState {
        ui::InternedId signal_iid;   ///< Resolved for zero-lookup sampling.
        std::string label;           ///< Display label.
        std::deque<float> samples;
    };
    std::unordered_map<editor::DocumentId, HoverState> hover_states_;

    static uint32_t color_for_index(size_t i);
};
