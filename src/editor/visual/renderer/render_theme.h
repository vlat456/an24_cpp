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

constexpr uint32_t COLOR_TEXT        = 0xFFDCD5D4;  // Text Primary
constexpr uint32_t COLOR_TEXT_DIM    = 0xFF968685;  // Text Secondary
constexpr uint32_t COLOR_WIRE        = 0xFF2A92C8;  // Wire Selected (Amber Bright)
constexpr uint32_t COLOR_WIRE_UNSEL  = 0xFF605048;  // Wire Inactive
constexpr uint32_t COLOR_WIRE_HOVER  = 0xFF3A6FA0;  // Wire Hovered (lighter blue)
constexpr uint32_t COLOR_WIRE_CURRENT= 0xFF2874A0;  // Wire Energized (Amber Mid)
constexpr uint32_t COLOR_GRID        = 0xFF312625;  // Grid Dot
constexpr uint32_t COLOR_SELECTED    = 0xFF2A92C8;  // Selected border (Amber Bright)
constexpr uint32_t COLOR_PORT_INPUT  = 0xFF986850;  // Port Current (generic input)
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFF5058B8;  // Port Voltage (generic output)
constexpr uint32_t COLOR_ROUTING_POINT = 0xFF54403E;  // Border Mid (#3E4054)
constexpr uint32_t COLOR_JUMP_ARC    = 0xFF605048;  // same as inactive wire
constexpr uint32_t COLOR_BODY_FILL   = 0xFF2C2322;  // Surface 1
constexpr uint32_t COLOR_HEADER_FILL = 0xFF352A29;  // Surface 2
constexpr uint32_t COLOR_BUS_FILL    = 0xFF3E3130;  // Surface 3
constexpr uint32_t COLOR_BUS_BORDER  = 0xFF54403E;  // Border Mid

constexpr float ARC_RADIUS_WORLD = 5.0f;
constexpr int   ARC_SEGMENTS     = 8;

// ---- Visual group ----
constexpr uint32_t COLOR_GROUP_FILL   = 0x30605048;  // Semi-transparent
constexpr uint32_t COLOR_GROUP_BORDER = 0xFF54403E;  // Border Mid
constexpr uint32_t COLOR_GROUP_TITLE  = 0xFFDCD5D4;  // Text Primary
constexpr uint32_t COLOR_RESIZE_HANDLE = 0xFF968685; // Text Secondary

// ---- Visual text node ----
constexpr uint32_t COLOR_TEXT_BORDER = 0x40968685;  // Faint border for text nodes

// ============================================================================
// Node style colors
// ============================================================================

struct NodeColors {
    uint32_t fill;
    uint32_t border;
};

inline NodeColors get_node_colors(const char* type_name) {
    static const std::unordered_map<std::string, NodeColors> styles = {
        {"battery",   {0xFF3E3130, 0xFF2A2B38}},  // olive-teal tint, dark border
        {"relay",     {0xFF2C3530, 0xFF1E2E2A}},  // dark teal
        {"lightbulb", {0xFF3E3220, 0xFF1E2814}},  // amber-warm
        {"pump",      {0xFF263040, 0xFF1A222E}},  // dark blue
        {"valve",     {0xFF263040, 0xFF2A1E1A}},  // same blue, warm border
        {"sensor",    {0xFF38262A, 0xFF261830}},  // rose-purple
        {"subsystem", {0xFF263838, 0xFF1A2A26}},  // teal
        {"motor",     {0xFF303020, 0xFF282818}},  // olive
        {"generator", {0xFF2C3820, 0xFF202A28}},  // green-gray
        {"switch",    {0xFF3C3220, 0xFF2A2418}},  // warm amber
        {"bus",       {0xFF303140, 0xFF20222E}},  // slate
        {"gyroscope", {0xFF30263A, 0xFF221630}},  // purple
        {"agk47",     {0xFF3A2220, 0xFF281412}},  // dark red
        {"refnode",   {0xFF222230, 0xFF141420}},  // near-black slate
    };

    if (!type_name || type_name[0] == '\0')
        return {0xFF2C2322, 0xFF1C1D24};  // Surface 1 fill, Surface 0 border

    std::string key = type_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    auto it = styles.find(key);
    return (it != styles.end()) ? it->second : NodeColors{0xFF2C2322, 0xFF1C1D24};  // Surface 1 fill, Surface 0 border
}

// ============================================================================
// Port type → color
// ============================================================================

inline uint32_t get_port_color(an24::PortType type) {
    switch (type) {
        case an24::PortType::V:           return 0xFF5068C0;  // Port Voltage  (warmer coral-red)
        case an24::PortType::I:           return 0xFF986850;  // Port Current  (muted blue-teal)
        case an24::PortType::Bool:        return 0xFF60905A;  // Port Bool     (muted green)
        case an24::PortType::RPM:         return 0xFF3088C0;  // Port RPM      (golden-amber)
        case an24::PortType::Temperature: return 0xFF4050B0;  // Port Temp     (warm rose)
        case an24::PortType::Pressure:    return 0xFF8C7848;  // Port Pressure (muted cyan-teal)
        case an24::PortType::Position:    return 0xFFA86078;  // Port Position (muted purple)
        case an24::PortType::Any:         return 0xFF968685;  // Port Any      (Text Secondary)
        default:                          return 0xFF968685;
    }
}

} // namespace render_theme
