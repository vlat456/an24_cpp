#pragma once

#include <cstdint>


enum class SplitterDirection { Horizontal, Vertical };

/// Reusable splitter widget for resizable panels.
class PanelSplitter {
public:
    struct Config {
        float thickness;
        float min_size;
        float max_size_ratio;
        
        Config() : thickness(4.0f), min_size(150.0f), max_size_ratio(0.5f) {}
    };
    
    PanelSplitter(SplitterDirection dir, float& size, Config config = Config())
        : direction_(dir), size_(size), config_(config) {}
    
    void render(float available_width, float available_height);
    float offset() const { return size_ + config_.thickness; }

private:
    SplitterDirection direction_;
    float& size_;
    Config config_;
    uint64_t id_ = 0;
    static uint64_t next_id_;
};

