#pragma once

#include "ui/math/pt.h"
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
    void sample(Document& doc, bool simulation_running);

    size_t max_samples() const { return max_samples_; }

    struct ChannelView {
        const OscilloscopeProbe* probe = nullptr;
        const std::deque<float>* samples = nullptr;
    };
    std::vector<ChannelView> channels() const;
    const std::deque<float>* samples_for_signal(const std::string& signal_key) const;

    struct SampleStats {
        bool has_value = false;
        float min_v = 0.0f;
        float max_v = 0.0f;
        float last_v = 0.0f;
    };
    static SampleStats compute_stats(const std::deque<float>& samples);

    const std::deque<float>& ensure_virtual_channel(Document& doc,
                                                    const std::string& signal_key,
                                                    bool simulation_running);
private:
    size_t max_samples_ = 1200;
    std::unordered_map<std::string, OscilloscopeProbe> probes_;
    std::unordered_map<std::string, std::deque<float>> samples_;
    std::unordered_map<std::string, std::deque<float>> virtual_samples_;

    static uint32_t color_for_index(size_t i);
};
