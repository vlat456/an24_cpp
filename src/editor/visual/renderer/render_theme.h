#pragma once

#include "json_parser/json_parser.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <algorithm>

// ============================================================================
// Color constants (ImGui byte order: 0xAABBGGRR)
// ============================================================================

namespace render_theme {

constexpr uint32_t COLOR_TEXT        = 0xFFFFFFFF;
constexpr uint32_t COLOR_TEXT_DIM    = 0xFFAAAAAA;
constexpr uint32_t COLOR_WIRE        = 0xFF50DCFF;  // gold — selected
constexpr uint32_t COLOR_WIRE_UNSEL  = 0xFF606060;  // grey — unselected
constexpr uint32_t COLOR_WIRE_CURRENT= 0xFF44AAFF;  // yellow — energized
constexpr uint32_t COLOR_GRID        = 0xFF404040;
constexpr uint32_t COLOR_SELECTED    = 0xFF00FF00;
constexpr uint32_t COLOR_PORT_INPUT  = 0xFFDCDCB4;
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;
constexpr uint32_t COLOR_ROUTING_POINT = 0xFFFF8000;
constexpr uint32_t COLOR_JUMP_ARC    = 0xFF404040;
constexpr uint32_t COLOR_BODY_FILL   = 0xFF303040;
constexpr uint32_t COLOR_HEADER_FILL = 0xFF282838;  // Slightly darker than body
constexpr uint32_t COLOR_BUS_FILL    = 0xFF404060;
constexpr uint32_t COLOR_BUS_BORDER  = 0xFF8080A0;

constexpr float ARC_RADIUS_WORLD = 5.0f;
constexpr int   ARC_SEGMENTS     = 8;

// ============================================================================
// Node style colors
// ============================================================================

struct NodeColors {
    uint32_t fill;
    uint32_t border;
};

inline NodeColors get_node_colors(const char* type_name) {
    static const std::unordered_map<std::string, NodeColors> styles = {
        {"battery",   {0xFF788C3C, 0xFF285028}},
        {"relay",     {0xFF328C78, 0xFF281E1E}},
        {"lightbulb", {0xFFF0A032, 0xFF1E5014}},
        {"pump",      {0xFF325A82, 0xFF1E3C5A}},
        {"valve",     {0xFF325A82, 0xFF5A3C1E}},
        {"sensor",    {0xFF824646, 0xFF462878}},
        {"subsystem", {0xFF328282, 0xFF1E5A46}},
        {"motor",     {0xFF646432, 0xFF46461E}},
        {"generator", {0xFF5A8232, 0xFF3C5050}},
        {"switch",    {0xFFF0Be32, 0xFF5A461E}},
        {"bus",       {0xFF505050, 0xFF323250}},
        {"gyroscope", {0xFF6E4A82, 0xFF4A2E5A}},
        {"agk47",     {0xFFBE5032, 0xFF7A321E}},
        {"refnode",   {0xFF323232, 0xFF1E1E1E}},
    };

    if (!type_name || type_name[0] == '\0')
        return {0xFF505050, 0xFF323232};

    std::string key = type_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    auto it = styles.find(key);
    return (it != styles.end()) ? it->second : NodeColors{0xFF505050, 0xFF323232};
}

// ============================================================================
// Port type → color
// ============================================================================

inline uint32_t get_port_color(an24::PortType type) {
    switch (type) {
        case an24::PortType::V:           return 0xFF0000FF;  // Red
        case an24::PortType::I:           return 0xFFFF0000;  // Blue
        case an24::PortType::Bool:        return 0xFF00FF00;  // Green
        case an24::PortType::RPM:         return 0xFF00A5FF;  // Orange
        case an24::PortType::Temperature: return 0xFF00FFFF;  // Yellow
        case an24::PortType::Pressure:    return 0xFFFFFF00;  // Cyan
        case an24::PortType::Position:    return 0xFF800080;  // Purple
        case an24::PortType::Any:         return 0xFF808080;  // Gray
        default:                          return 0xFF808080;
    }
}

} // namespace render_theme
