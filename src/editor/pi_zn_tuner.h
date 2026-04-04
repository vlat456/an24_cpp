#pragma once

#include <optional>
#include <string>
#include <vector>

class Document;

struct ZNTuneConfig {
    std::string pi_node;
    std::string feedback_signal;
    float dt_sec = 1.0f / 60.0f;
    float run_time_sec = 16.0f;
    float settle_time_sec = 3.0f;
    float kp_lo = 0.01f;
    float kp_hi = 80.0f;
    int max_expand = 10;
    int binary_iters = 14;
    int min_peaks = 4;
};

struct ZNTuneResult {
    bool ok = false;
    std::string error;
    float Ku = 0.0f;
    float Tu = 0.0f;
    float Kp = 0.0f;
    float Ki = 0.0f;
};

bool zn_is_sustained_oscillation(const std::vector<float>& y, int min_peaks = 6);
std::optional<float> zn_estimate_tu(const std::vector<float>& y, float dt_sec);

ZNTuneResult tune_pi_ziegler_nichols(Document& doc, const ZNTuneConfig& cfg, bool apply_result_to_pi = true);
bool apply_pi_params(Document& doc, const std::string& pi_node, float Kp, float Ki, std::string* error_out = nullptr);
