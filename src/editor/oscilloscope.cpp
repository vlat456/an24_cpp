#include "oscilloscope.h"

#include "document.h"
#include "editor/visual/wire/wire.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

/// Resolve the signal InternedId for a wire probe. Returns false if wire
/// has no resolvable signal key (e.g. wire deleted from blueprint).
bool resolve_probe_signal(Document& doc,
                                 const WindowScopeId& scope_id,
                                 core::InternedId wire_iid,
                                 core::InternedId& out_key,
                                 std::string& out_label) {
    const std::string_view wire_sv = doc.interner().resolve(wire_iid);
    if (wire_sv.empty()) return false;
    core::InternedId key_iid = doc.resolve_wire_signal_key(scope_id, wire_sv);
    if (key_iid.empty()) return false;
    out_key = key_iid;
    out_label = std::string{wire_sv};
    return true;
}

/// Find the world-space anchor point on a wire's polyline.
bool resolve_probe_anchor(Document& doc,
                                 core::InternedId wire_iid,
                                 const WindowScopeId& scope_id,
                                 const ui::Pt* preferred_world,
                                 ui::Pt& out_world) {
    const std::string_view wire_sv = doc.interner().resolve(wire_iid);

    BlueprintWindow* win = nullptr;
    for (auto& wptr : doc.windowManager().windows()) {
        if (wptr && wptr->resolved_scope_id() == scope_id) {
            win = wptr.get();
            break;
        }
    }
    if (!win) return false;
    auto* vw = dynamic_cast<visual::Wire*>(win->scene.find(wire_sv));
    if (!vw) return false;
    const auto& poly = vw->polyline();
    if (poly.size() < 2) return false;
    if (!preferred_world) {
        size_t mid = poly.size() / 2;
        out_world = poly[mid];
        return true;
    }

    ui::Pt best = poly.front();
    float best_d2 = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        const ui::Pt a = poly[i];
        const ui::Pt b = poly[i + 1];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len2 = dx * dx + dy * dy;
        float t = 0.0f;
        if (len2 > 1e-6f) {
            t = ((preferred_world->x - a.x) * dx + (preferred_world->y - a.y) * dy) / len2;
            t = std::clamp(t, 0.0f, 1.0f);
        }
        const ui::Pt p(a.x + t * dx, a.y + t * dy);
        const float ddx = p.x - preferred_world->x;
        const float ddy = p.y - preferred_world->y;
        const float d2 = ddx * ddx + ddy * ddy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = p;
        }
    }
    out_world = best;
    return true;
}

} // namespace

// =============================================================================
// OscilloscopeModel — partition lookup
// =============================================================================

OscilloscopeModel::DocumentProbes* OscilloscopeModel::find_doc(const editor::DocumentId& doc_id) {
    auto it = docs_.find(doc_id);
    return (it != docs_.end()) ? &it->second : nullptr;
}

const OscilloscopeModel::DocumentProbes* OscilloscopeModel::find_doc(const editor::DocumentId& doc_id) const {
    auto it = docs_.find(doc_id);
    return (it != docs_.end()) ? &it->second : nullptr;
}

// =============================================================================
// OscilloscopeModel — color
// =============================================================================

uint32_t OscilloscopeModel::color_for_index(size_t i) {
    auto rgba = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t {
        return static_cast<uint32_t>(r)
             | (static_cast<uint32_t>(g) << 8)
             | (static_cast<uint32_t>(b) << 16)
             | (static_cast<uint32_t>(a) << 24);
    };
    static const uint32_t k[] = {
        rgba(244, 114, 182, 255),
        rgba(56, 189, 248, 255),
        rgba(34, 197, 94, 255),
        rgba(251, 191, 36, 255),
        rgba(248, 113, 113, 255),
        rgba(129, 140, 248, 255),
    };
    return k[i % (sizeof(k) / sizeof(k[0]))];
}

// =============================================================================
// OscilloscopeModel — probe lifecycle
// =============================================================================

void OscilloscopeModel::toggle_probe(Document& doc,
                                     const WindowScopeId& scope_id,
                                     core::InternedId wire_iid,
                                     const ui::Pt* click_world) {
    if (wire_iid.empty()) return;

    auto& partition = docs_[doc.id()];
    ProbeKey key{scope_id, wire_iid};

    auto it = partition.probes.find(key);
    if (it != partition.probes.end()) {
        partition.probes.erase(it);
        return;
    }

    OscilloscopeProbe p;
    p.wire_iid = wire_iid;
    if (!resolve_probe_signal(doc, scope_id, wire_iid, p.signal_iid, p.label)) return;
    if (!resolve_probe_anchor(doc, wire_iid, scope_id, click_world, p.world_pos)) return;

    // Per-document color assignment: count existing probes.
    p.color = color_for_index(partition.probes.size());

    partition.probes.emplace(key, std::move(p));
}

void OscilloscopeModel::purge_for(const editor::DocumentId& doc_id) {
    docs_.erase(doc_id);
    hover_states_.erase(doc_id);
}

void OscilloscopeModel::purge_all() {
    docs_.clear();
    hover_states_.clear();
}

// =============================================================================
// OscilloscopeModel — blueprint rebuild
// =============================================================================

void OscilloscopeModel::on_blueprint_changed(Document& doc) {
    auto* partition = find_doc(doc.id());
    if (!partition) return;
    if (partition->probes.empty()) return;

    // When simulation is stopped, signal key resolution is impossible
    // (the simulation's signal_key_interner is empty). Skip re-resolution
    // — probes retain their stale signal_iid until next sim start.
    // Anchor re-resolution is independent of sim state (depends on wire geometry).
    const bool sim_running = doc.isSimulationRunning();

    // Collect keys to remove (can't erase while iterating).
    std::vector<ProbeKey> to_remove;

    for (auto& [key, probe] : partition->probes) {
        // Re-resolve signal key only while simulation is running.
        if (sim_running) {
            if (!resolve_probe_signal(doc, key.scope_id, key.wire_iid,
                                      probe.signal_iid, probe.label)) {
                to_remove.push_back(key);
                continue;
            }
        }

        // Re-resolve anchor position — depends on wire geometry, not sim state.
        const ui::Pt old_pos = probe.world_pos;
        if (!resolve_probe_anchor(doc, key.wire_iid, key.scope_id,
                                  &old_pos, probe.world_pos)) {
            to_remove.push_back(key);
            continue;
        }
    }

    for (const auto& key : to_remove) {
        partition->probes.erase(key);
    }

    // NOTE: Hover state is NOT invalidated here. Hover is transient UI state
    // managed exclusively by the render phase (clear → hit-test → set per frame).
    // Invalidating it here caused samples to be cleared by sample() before
    // renderTooltips() could re-set the signal_iid. See #375.
}

// =============================================================================
// OscilloscopeModel — sampling
// =============================================================================

void OscilloscopeModel::sample(Document& doc, bool simulation_running, float sample_dt_sec) {
    if (sample_dt_sec > 0.0f) sample_period_sec_ = sample_dt_sec;

    // -- Probe sampling (gated on partition existence) --
    auto* partition = find_doc(doc.id());
    if (partition) {
        for (auto& [key, probe] : partition->probes) {
            float v = 0.0f;
            if (simulation_running && !probe.signal_iid.empty()) {
                v = doc.get_signal_value(probe.signal_iid);
            }
            probe.samples.push_back(v);
            while (probe.samples.size() > max_samples_) probe.samples.pop_front();
        }
    }

    // -- Hover sampling (always runs, independent of probe partition) --
    auto hover_it = hover_states_.find(doc.id());
    if (hover_it != hover_states_.end() && !hover_it->second.signal_iid.empty()) {
        float v = 0.0f;
        if (simulation_running) {
            v = doc.get_signal_value(hover_it->second.signal_iid);
        }
        hover_it->second.samples.push_back(v);
        while (hover_it->second.samples.size() > max_samples_) hover_it->second.samples.pop_front();
    } else if (hover_it != hover_states_.end()) {
        hover_it->second.samples.clear();
    }
}

// =============================================================================
// OscilloscopeModel — hover state
// =============================================================================

void OscilloscopeModel::set_hover_signal(const editor::DocumentId& doc_id, core::InternedId signal_iid) {
    auto& state = hover_states_[doc_id];
    state.signal_iid = signal_iid;
}

void OscilloscopeModel::clear_hover_signal(const editor::DocumentId& doc_id) {
    auto it = hover_states_.find(doc_id);
    if (it != hover_states_.end()) {
        it->second.signal_iid = core::InternedId{};
    }
}

const std::deque<float>& OscilloscopeModel::hover_samples(const editor::DocumentId& doc_id) const {
    static const std::deque<float> empty;
    auto it = hover_states_.find(doc_id);
    return (it != hover_states_.end()) ? it->second.samples : empty;
}

core::InternedId OscilloscopeModel::hover_signal_key(const editor::DocumentId& doc_id) const {
    auto it = hover_states_.find(doc_id);
    return (it != hover_states_.end()) ? it->second.signal_iid : core::InternedId{};
}

void OscilloscopeModel::purge_hover_for(const editor::DocumentId& doc_id) {
    hover_states_.erase(doc_id);
}

// =============================================================================
// OscilloscopeModel — channel access
// =============================================================================

std::vector<OscilloscopeModel::ChannelView> OscilloscopeModel::channels_for(
        const editor::DocumentId& document_id) const {
    const auto* partition = find_doc(document_id);
    if (!partition) return {};

    std::vector<ChannelView> out;
    out.reserve(partition->probes.size());
    for (const auto& [key, probe] : partition->probes) {
        out.push_back(ChannelView{&probe});
    }
    // Sort by label for predictable user-visible ordering.
    std::sort(out.begin(), out.end(), [](const ChannelView& a, const ChannelView& b) {
        return a.probe->label < b.probe->label;
    });
    return out;
}

void OscilloscopeModel::for_each_probe_in_scope(
        const editor::DocumentId& doc_id,
        const WindowScopeId& scope_id,
        const std::function<void(const OscilloscopeProbe&)>& fn) const {
    const auto* partition = find_doc(doc_id);
    if (!partition) return;
    for (const auto& [key, probe] : partition->probes) {
        if (key.scope_id == scope_id) {
            fn(probe);
        }
    }
}
