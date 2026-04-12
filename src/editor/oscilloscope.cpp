#include "oscilloscope.h"

#include "document.h"
#include "editor/visual/wire/wire.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

std::string make_probe_id(const WindowScopeId& scope_id, std::string_view wire_id) {
    std::string probe_id;
    probe_id.reserve(scope_id.key().size() + wire_id.size() + 8);
    if (scope_id.is_root()) {
        probe_id.append("root:");
    } else if (scope_id.is_embedded()) {
        probe_id.append("emb:");
    } else {
        probe_id.append("ext:");
    }
    probe_id.append(scope_id.key());
    probe_id.push_back('|');
    probe_id.append(wire_id);
    return probe_id;
}

static bool resolve_probe_signal(Document& doc,
                                 const WindowScopeId& scope_id,
                                 std::string_view wire_id,
                                 std::string& out_key,
                                 std::string& out_label) {
    out_key = doc.resolve_wire_signal_key(scope_id, wire_id);
    if (out_key.empty()) return false;
    out_label = out_key;
    return true;
}

static bool resolve_probe_anchor(Document& doc,
                                 std::string_view wire_id,
                                 const WindowScopeId& scope_id,
                                 const ui::Pt* preferred_world,
                                 ui::Pt& out_world) {
    BlueprintWindow* win = nullptr;
    for (auto& wptr : doc.windowManager().windows()) {
        if (wptr && wptr->resolved_scope_id() == scope_id) {
            win = wptr.get();
            break;
        }
    }
    if (!win) return false;
    auto* vw = dynamic_cast<visual::Wire*>(win->scene.find(wire_id));
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

void OscilloscopeModel::toggle_probe(Document& doc,
                                     const WindowScopeId& scope_id,
                                     const std::string& wire_id,
                                     const ui::Pt* click_world) {
    if (wire_id.empty()) return;
    const std::string probe_id = make_probe_id(scope_id, wire_id);
    auto it = probes_.find(probe_id);
    if (it != probes_.end()) {
        probes_.erase(it);
        samples_.erase(probe_id);
        return;
    }

    OscilloscopeProbe p;
    p.probe_id = probe_id;
    p.wire_id = wire_id;
    p.doc_id = doc.id();
    p.scope_id = scope_id;
    if (!resolve_probe_signal(doc, scope_id, wire_id, p.signal_key, p.label)) return;
    if (!resolve_probe_anchor(doc, wire_id, scope_id, click_world, p.world_pos)) return;
    p.color = color_for_index(probes_.size());

    probes_[probe_id] = p;
    samples_[probe_id] = std::deque<float>{};
}

void OscilloscopeModel::remove_probe(const std::string& probe_id) {
    probes_.erase(probe_id);
    samples_.erase(probe_id);
}

bool OscilloscopeModel::has_probe(const std::string& probe_id) const {
    return probes_.find(probe_id) != probes_.end();
}

const OscilloscopeProbe* OscilloscopeModel::probe(const std::string& probe_id) const {
    auto it = probes_.find(probe_id);
    return (it == probes_.end()) ? nullptr : &it->second;
}

void OscilloscopeModel::on_blueprint_changed(Document& doc) {
    std::vector<std::string> to_remove;
    std::vector<std::pair<std::string, OscilloscopeProbe>> updates;
    for (const auto& [probe_id, p] : probes_) {
        OscilloscopeProbe updated = p;
        if (!resolve_probe_signal(doc, p.scope_id, p.wire_id, updated.signal_key, updated.label)) {
            to_remove.push_back(probe_id);
            continue;
        }

        if (!resolve_probe_anchor(doc, p.wire_id, p.scope_id, &p.world_pos, updated.world_pos)) {
            to_remove.push_back(probe_id);
            continue;
        }
        updates.emplace_back(probe_id, std::move(updated));
    }
    for (auto& [probe_id, updated] : updates) {
        auto it = probes_.find(probe_id);
        if (it != probes_.end()) it->second = std::move(updated);
    }
    for (const auto& id : to_remove) {
        remove_probe(id);
    }
}

void OscilloscopeModel::sample(Document& doc, bool simulation_running, float sample_dt_sec) {
    if (sample_dt_sec > 0.0f) sample_period_sec_ = sample_dt_sec;
    for (auto& [probe_id, p] : probes_) {
        auto& q = samples_[probe_id];
        float v = simulation_running ? doc.simulation().get_wire_voltage(p.signal_key) : 0.0f;
        q.push_back(v);
        while (q.size() > max_samples_) q.pop_front();
    }

    if (!hover_signal_key_.empty()) {
        const float v = simulation_running ? doc.simulation().get_wire_voltage(hover_signal_key_) : 0.0f;
        hover_samples_.push_back(v);
        while (hover_samples_.size() > max_samples_) hover_samples_.pop_front();
    } else {
        hover_samples_.clear();
    }
}

std::vector<OscilloscopeModel::ChannelView> OscilloscopeModel::channels() const {
    std::vector<ChannelView> out;
    out.reserve(probes_.size());
    for (const auto& [probe_id, p] : probes_) {
        auto it = samples_.find(probe_id);
        if (it == samples_.end()) continue;
        out.push_back(ChannelView{&p, &it->second});
    }
    std::sort(out.begin(), out.end(), [](const ChannelView& a, const ChannelView& b) {
        return a.probe->probe_id < b.probe->probe_id;
    });
    return out;
}
