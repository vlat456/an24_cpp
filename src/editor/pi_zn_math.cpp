#include "pi_zn_tuner.h"

#include <algorithm>
#include <cmath>

namespace {

struct Peaks {
    std::vector<int> idx;
    std::vector<float> val;
};

static Peaks find_peaks(const std::vector<float>& y, float prominence_frac = 0.05f) {
    Peaks p;
    if (y.size() < 5) return p;

    auto [mn_it, mx_it] = std::minmax_element(y.begin(), y.end());
    const float range = *mx_it - *mn_it;
    const float mid = 0.5f * (*mx_it + *mn_it);
    const float amp_gate = std::max(1e-6f, range * std::max(0.02f, prominence_frac));

    for (int i = 1; i + 1 < static_cast<int>(y.size()); ++i) {
        const float a = y[i - 1];
        const float b = y[i];
        const float c = y[i + 1];
        if (!(b >= a && b > c)) continue;
        if ((b - mid) < amp_gate) continue;
        if (!p.idx.empty() && (i - p.idx.back()) < 3) continue;
        p.idx.push_back(i);
        p.val.push_back(b);
    }
    return p;
}

} // namespace

bool zn_is_sustained_oscillation(const std::vector<float>& y, int min_peaks) {
    Peaks pk = find_peaks(y);
    if (static_cast<int>(pk.idx.size()) < min_peaks) return false;

    const int start = static_cast<int>(y.size() / 3);
    float mean = 0.0f;
    int n = 0;
    for (int i = start; i < static_cast<int>(y.size()); ++i) {
        mean += y[i];
        ++n;
    }
    mean = (n > 0) ? (mean / n) : 0.0f;

    std::vector<float> amp;
    amp.reserve(pk.val.size());
    for (float v : pk.val) amp.push_back(std::fabs(v - mean));
    if (static_cast<int>(amp.size()) < min_peaks) return false;

    const int m = std::min(4, static_cast<int>(amp.size() / 2));
    if (m < 2) return false;

    float early = 0.0f;
    float late = 0.0f;
    for (int i = 0; i < m; ++i) early += amp[i];
    for (int i = 0; i < m; ++i) late += amp[static_cast<int>(amp.size()) - m + i];
    early /= static_cast<float>(m);
    late /= static_cast<float>(m);

    if (early < 1e-6f) return false;
    const float ratio = late / early;
    return ratio > 0.85f && ratio < 1.20f;
}

std::optional<float> zn_estimate_tu(const std::vector<float>& y, float dt_sec) {
    Peaks pk = find_peaks(y);
    if (pk.idx.size() < 2) return std::nullopt;

    const int intervals = std::min(static_cast<int>(pk.idx.size() - 1), 6);
    const int from = static_cast<int>(pk.idx.size()) - 1 - intervals;
    float sum = 0.0f;
    for (int i = from + 1; i < static_cast<int>(pk.idx.size()); ++i) {
        sum += static_cast<float>(pk.idx[i] - pk.idx[i - 1]) * dt_sec;
    }
    if (intervals <= 0) return std::nullopt;
    return sum / static_cast<float>(intervals);
}
