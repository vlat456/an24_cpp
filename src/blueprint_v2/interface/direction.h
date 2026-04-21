#pragma once

#include <cstdint>

namespace bp2 {

enum class Direction : uint8_t {
    Input,
    Output,
    InOut
};

enum class BridgeDirection : uint8_t {
    Input,
    Output,
};

constexpr Direction to_port_direction(BridgeDirection direction) {
    return direction == BridgeDirection::Input ? Direction::Input : Direction::Output;
}

} // namespace bp2
