#include "oscilloscope.h"

#include "document.h"
#include "editor/visual/wire/wire.h"
#include "blueprint_v2/path/path.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

static bool decode_source_key(const bp2::Blueprint::Wire& w,
                              const bp2::PathArena& arena,
                              ui::StringInterner& interner,
                              std::string& out_key,
                              std::string& out_label) {
    if (w.source.kind() != bp2::PathKind::Port) return false;
    const ui::InternedId port_iid = w.source.segment();
    const bp2::Path node_path = arena.parent(w.source);
    if (node_path.kind() != bp2::PathKind::Node) return false;
    const ui::InternedId node_iid = node_path.segment();
    const std::string node = std::string(interner.resolve(node_iid));
    const std::string port = std::string(interner.resolve(port_iid));
    if (node.empty() || port.empty()) return false;
    out_key = node + "." + port;
    out_label = out_key;
    return true;
}

static bool resolve_probe_anchor(Document& doc,
                                 const bp2::Blueprint::Wire& w,
                                 const std::string& group_id,
                                 const ui::Pt* preferred_world,
                                 ui::Pt& out_world) {
    auto* win = doc.windowManager().find(group_id);
    if (!win) return false;
    auto* vw = dynamic_cast<visual::Wire*>(win->scene.find(doc.interner().resolve(w.id)));
    if (!vw) return false;
    const auto& poly = vw->polyline();
    if (poly.size() < 2) return false;
    if (!preferred_world) {
        size_t mid = poly.size() / 2;
        if (mid >= poly.size()) mid = poly.size() - 1;
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
                                     const std::string& group_id,
                                     const std::string& wire_id,
                                     const ui::Pt* click_world) {
    if (wire_id.empty()) return;
    auto it = probes_.find(wire_id);
    if (it != probes_.end()) {
        probes_.erase(it);
        samples_.erase(wire_id);
        return;
    }

    ui::InternedId wid = doc.interner().lookup(wire_id);
    if (wid.empty()) return;
    const bp2::Blueprint::Wire* w = doc.blueprint().find_wire(wid);
    if (!w) return;

    OscilloscopeProbe p;
    p.wire_id = wire_id;
    p.doc_id = doc.id();
    p.group_id = group_id;
    if (!decode_source_key(*w, doc.arena(), doc.interner(), p.signal_key, p.label)) return;
    if (!resolve_probe_anchor(doc, *w, group_id, click_world, p.world_pos)) return;
    p.color = color_for_index(probes_.size());

    probes_[wire_id] = p;
    samples_[wire_id] = std::deque<float>{};
}

void OscilloscopeModel::remove_probe(const std::string& wire_id) {
    probes_.erase(wire_id);
    samples_.erase(wire_id);
}

bool OscilloscopeModel::has_probe(const std::string& wire_id) const {
    return probes_.find(wire_id) != probes_.end();
}

const OscilloscopeProbe* OscilloscopeModel::probe(const std::string& wire_id) const {
    auto it = probes_.find(wire_id);
    return (it == probes_.end()) ? nullptr : &it->second;
}

void OscilloscopeModel::on_blueprint_changed(Document& doc) {
    std::vector<std::string> to_remove;
    std::vector<std::pair<std::string, OscilloscopeProbe>> updates;
    for (const auto& [wire_id, p] : probes_) {
        ui::InternedId wid = doc.interner().lookup(wire_id);
        const bp2::Blueprint::Wire* w = wid.empty() ? nullptr : doc.blueprint().find_wire(wid);
        if (!w) {
            to_remove.push_back(wire_id);
            continue;
        }

        OscilloscopeProbe updated = p;
        if (!decode_source_key(*w, doc.arena(), doc.interner(), updated.signal_key, updated.label)) {
            to_remove.push_back(wire_id);
            continue;
        }

        if (!resolve_probe_anchor(doc, *w, p.group_id, &p.world_pos, updated.world_pos)) {
            to_remove.push_back(wire_id);
            continue;
        }
        updates.emplace_back(wire_id, std::move(updated));
    }
    for (auto& [wire_id, updated] : updates) {
        auto it = probes_.find(wire_id);
        if (it != probes_.end()) it->second = std::move(updated);
    }
    for (const auto& id : to_remove) {
        remove_probe(id);
    }
}

void OscilloscopeModel::sample(Document& doc, bool simulation_running) {
    for (auto& [wire_id, p] : probes_) {
        auto& q = samples_[wire_id];
        float v = simulation_running ? doc.simulation().get_wire_voltage(p.signal_key) : 0.0f;
        q.push_back(v);
        while (q.size() > max_samples_) q.pop_front();
    }
}

std::vector<OscilloscopeModel::ChannelView> OscilloscopeModel::channels() const {
    std::vector<ChannelView> out;
    out.reserve(probes_.size());
    for (const auto& [wire_id, p] : probes_) {
        auto it = samples_.find(wire_id);
        if (it == samples_.end()) continue;
        out.push_back(ChannelView{&p, &it->second});
    }
    std::sort(out.begin(), out.end(), [](const ChannelView& a, const ChannelView& b) {
        return a.probe->wire_id < b.probe->wire_id;
    });
    return out;
}

OscilloscopeModel::SampleStats OscilloscopeModel::compute_stats(const std::deque<float>& samples) {
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
    return s;
}

const std::deque<float>& OscilloscopeModel::ensure_virtual_channel(Document& doc,
                                                                    const std::string& signal_key,
                                                                    bool simulation_running) {
    auto& q = virtual_samples_[signal_key];
    const float v = simulation_running ? doc.simulation().get_wire_voltage(signal_key) : 0.0f;
    q.push_back(v);
    while (q.size() > max_samples_) q.pop_front();
    return q;
}
