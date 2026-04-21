#pragma once

#include "editor/data/node_content.h"
#include "blueprint_v2/blueprint/node_port.h"
#include "ui/core/interned_id.h"
#include <array>
#include <vector>
#include <string_view>
#include <algorithm>

/// Intermediate struct used during layout resolution.
struct ResolvedPort {
    std::string_view port_name;
    PortType type;
    bp2::Direction logical_direction;
    bp2::PortLayoutSide layout_side;
    uint8_t final_position = 0;
    
    /// Position hint from override (255 = auto, no hint).
    /// Used only during resolution; not meaningful after resolve_port_layout() returns.
    uint8_t position_hint = 255;
};

/// Four-sided resolved layout with named access
struct ResolvedLayout {
    std::vector<ResolvedPort> left;
    std::vector<ResolvedPort> right;
    std::vector<ResolvedPort> top;
    std::vector<ResolvedPort> bottom;
    
    std::vector<ResolvedPort>& operator[](bp2::PortLayoutSide side) {
        switch (side) {
            case bp2::PortLayoutSide::Left:   return left;
            case bp2::PortLayoutSide::Right:  return right;
            case bp2::PortLayoutSide::Top:    return top;
            case bp2::PortLayoutSide::Bottom: return bottom;
        }
        return left;
    }
    
    const std::vector<ResolvedPort>& operator[](bp2::PortLayoutSide side) const {
        switch (side) {
            case bp2::PortLayoutSide::Left:   return left;
            case bp2::PortLayoutSide::Right:  return right;
            case bp2::PortLayoutSide::Top:    return top;
            case bp2::PortLayoutSide::Bottom: return bottom;
        }
        return left;
    }
};

/// Resolve port layout from inputs, outputs, and overrides.
/// Returns ports grouped by geometric side with assigned positions.
///
/// Algorithm:
/// 1. Build flat list of all ports with default sides (Input→Left, Output→Right)
/// 2. Apply overrides by name match
/// 3. Group ports by final side
/// 4. Within each side: sort by position hint (overridden first), then append auto ports
/// 5. Assign sequential final_position values
inline ResolvedLayout resolve_port_layout(
    const std::vector<bp2::PortDescriptor>& inputs,
    const std::vector<bp2::PortDescriptor>& outputs,
    const std::vector<PortLayoutOverride>& overrides,
    const ui::StringInterner& interner)
{
    // Step 1: Build flat list with default sides.
    // InOut ports appear in both inputs and outputs (see document.cpp);
    // deduplicate by name so each physical port has exactly one entry.
    std::vector<ResolvedPort> all_ports;
    
    for (const auto& p : inputs) {
        ResolvedPort rp;
        rp.port_name = interner.resolve(p.name);
        rp.type = p.port_type;
        rp.logical_direction = p.direction;
        rp.layout_side = bp2::default_layout_side(p.direction);
        rp.final_position = 255;  // Will be assigned later
        all_ports.push_back(rp);
    }
    
    for (const auto& p : outputs) {
        std::string_view name = interner.resolve(p.name);
        // Skip if already added from inputs (InOut port deduplication)
        bool duplicate = false;
        for (const auto& existing : all_ports) {
            if (existing.port_name == name) { duplicate = true; break; }
        }
        if (duplicate) continue;
        
        ResolvedPort rp;
        rp.port_name = name;
        rp.type = p.port_type;
        rp.logical_direction = p.direction;
        rp.layout_side = bp2::default_layout_side(p.direction);
        rp.final_position = 255;
        all_ports.push_back(rp);
    }
    
    // Step 2: Apply overrides by name match
    for (const auto& ov : overrides) {
        for (auto& rp : all_ports) {
            if (rp.port_name == ov.port_name) {
                if (ov.side.has_value()) {
                    rp.layout_side = *ov.side;
                }
                if (ov.position.has_value()) {
                    rp.position_hint = *ov.position;
                }
            }
        }
    }
    
    // Step 3: Group by final side
    ResolvedLayout layout;
    for (auto& rp : all_ports) {
        layout[rp.layout_side].push_back(rp);
    }
    
    // Step 4: Sort each side - hinted ports first (by hint), then auto ports
    for (auto side : {bp2::PortLayoutSide::Left, bp2::PortLayoutSide::Right, 
                      bp2::PortLayoutSide::Top, bp2::PortLayoutSide::Bottom}) {
        auto& side_ports = layout[side];
        // Partition: hinted (position_hint != 255) vs auto (position_hint == 255)
        auto hinted_end = std::stable_partition(side_ports.begin(), side_ports.end(),
            [](const ResolvedPort& p) { return p.position_hint != 255; });
        
        // Sort hinted by position hint
        std::stable_sort(side_ports.begin(), hinted_end,
            [](const ResolvedPort& a, const ResolvedPort& b) {
                return a.position_hint < b.position_hint;
            });
        
        // Step 5: Assign sequential final_position
        for (size_t i = 0; i < side_ports.size(); ++i) {
            side_ports[i].final_position = static_cast<uint8_t>(i);
        }
    }
    
    return layout;
}
