#include "pi_zn_tuner.h"

#include "document.h"

#include <algorithm>
#include <cmath>

namespace {

static std::vector<float> run_closed_loop(Document& doc,
                                          ui::InternedId node_id,
                                          ui::InternedId kp_key,
                                          ui::InternedId ki_key,
                                          const std::string& feedback_signal,
                                          float kp,
                                          float dt_sec,
                                          float run_time_sec,
                                          float settle_time_sec) {
    doc.model().update_node(node_id, [&](bp2::Blueprint::Node& n) {
        n.semantic.params[kp_key] = kp;
        n.semantic.params[ki_key] = 0.0f;
    });

    doc.stopSimulation();
    doc.startSimulation();

    const int steps_total = std::max(1, static_cast<int>(std::round(run_time_sec / dt_sec)));
    const int steps_skip = std::max(0, static_cast<int>(std::round(settle_time_sec / dt_sec)));

    std::vector<float> y;
    y.reserve(static_cast<size_t>(std::max(0, steps_total - steps_skip)));

    for (int i = 0; i < steps_total; ++i) {
        doc.updateSimulationStep(dt_sec);
        if (i >= steps_skip) {
            y.push_back(doc.simulation().get_wire_voltage(feedback_signal));
        }
    }

    doc.stopSimulation();
    return y;
}

} // namespace

ZNTuneResult tune_pi_ziegler_nichols(Document& doc, const ZNTuneConfig& cfg, bool apply_result_to_pi) {
    ZNTuneResult out;

    ui::InternedId node_id = doc.interner().lookup(cfg.pi_node);
    if (node_id.empty() || !doc.blueprint().find_node(node_id)) {
        out.error = "PI node not found";
        return out;
    }

    ui::InternedId kp_key = doc.interner().intern("Kp");
    ui::InternedId ki_key = doc.interner().intern("Ki");

    const bp2::Blueprint original_bp = doc.model().current();

    const bp2::Blueprint::Node* pi_node = doc.blueprint().find_node(node_id);
    if (!pi_node) {
        out.error = "PI node not found";
        return out;
    }
    float original_kp = 0.0f;
    float original_ki = 0.0f;
    if (auto it = pi_node->semantic.params.find(kp_key); it != pi_node->semantic.params.end()) original_kp = it->second;
    if (auto it = pi_node->semantic.params.find(ki_key); it != pi_node->semantic.params.end()) original_ki = it->second;

    const bool was_running = doc.isSimulationRunning();

    float kp_lo = cfg.kp_lo;
    float kp_hi = cfg.kp_hi;

    bool lo_osc = zn_is_sustained_oscillation(
        run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                        kp_lo, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec),
        cfg.min_peaks);
    bool hi_osc = zn_is_sustained_oscillation(
        run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                        kp_hi, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec),
        cfg.min_peaks);

    int expand = 0;
    while (!hi_osc && expand < cfg.max_expand) {
        kp_hi *= 2.0f;
        hi_osc = zn_is_sustained_oscillation(
            run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                            kp_hi, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec),
            cfg.min_peaks);
        ++expand;
    }
    if (!hi_osc) {
        doc.model().replace_current(bp2::Blueprint(original_bp));
        if (was_running) doc.startSimulation();
        out.error = "Failed to find oscillatory upper bound for Ku";
        return out;
    }

    while (lo_osc && kp_lo > 1e-5f) {
        kp_lo *= 0.5f;
        lo_osc = zn_is_sustained_oscillation(
            run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                            kp_lo, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec),
            cfg.min_peaks);
    }

    for (int it = 0; it < cfg.binary_iters; ++it) {
        const float mid = 0.5f * (kp_lo + kp_hi);
        const bool mid_osc = zn_is_sustained_oscillation(
            run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                            mid, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec),
            cfg.min_peaks);
        if (mid_osc) kp_hi = mid;
        else kp_lo = mid;
    }

    const float Ku = kp_hi;
    const std::vector<float> y_ku = run_closed_loop(doc, node_id, kp_key, ki_key, cfg.feedback_signal,
                                                    Ku, cfg.dt_sec, cfg.run_time_sec, cfg.settle_time_sec);
    const auto tu_opt = zn_estimate_tu(y_ku, cfg.dt_sec);
    if (!tu_opt.has_value()) {
        doc.model().replace_current(bp2::Blueprint(original_bp));
        if (was_running) doc.startSimulation();
        out.error = "Failed to estimate Tu from oscillation run";
        return out;
    }

    const float Tu = *tu_opt;
    const float Kp = 0.45f * Ku;
    const float Ki = 1.2f * Kp / std::max(Tu, 1e-6f);

    if (apply_result_to_pi) {
        doc.model().replace_current(bp2::Blueprint(original_bp));
        doc.model().push_checkpoint();
        execute(doc.model(), doc.interner(), cmd_set_param(node_id, kp_key, Kp));
        execute(doc.model(), doc.interner(), cmd_set_param(node_id, ki_key, Ki));
        doc.rebuildAllWindows();
    } else {
        doc.model().replace_current(bp2::Blueprint(original_bp));
    }

    if (was_running) doc.startSimulation();

    out.ok = true;
    out.Ku = Ku;
    out.Tu = Tu;
    out.Kp = Kp;
    out.Ki = Ki;
    return out;
}

bool apply_pi_params(Document& doc, const std::string& pi_node, float Kp, float Ki, std::string* error_out) {
    ui::InternedId node_id = doc.interner().lookup(pi_node);
    if (node_id.empty() || !doc.blueprint().find_node(node_id)) {
        if (error_out) *error_out = "PI node not found";
        return false;
    }
    ui::InternedId kp_key = doc.interner().intern("Kp");
    ui::InternedId ki_key = doc.interner().intern("Ki");

    doc.model().push_checkpoint();
    execute(doc.model(), doc.interner(), cmd_set_param(node_id, kp_key, Kp));
    execute(doc.model(), doc.interner(), cmd_set_param(node_id, ki_key, Ki));
    doc.rebuildAllWindows();
    return true;
}
