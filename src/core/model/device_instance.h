#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <utility>

#include "core/model/port.h"

struct DeviceInstance {
    std::string name;
    std::string template_name;
    std::string classname;
    std::string display_name;
    std::string priority = "med";
    std::optional<size_t> bucket;
    bool critical = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::optional<std::pair<float,float>> pos;
    std::optional<std::pair<float,float>> size;

    DeviceInstance() = default;

    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_ = {},
        std::unordered_map<std::string, bp2::Direction> ports_ = {}
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        for (const auto& [port_name, direction] : ports_) {
            // No heuristic guessing — PortType::Any is the safe default.
            // Tests needing specific types should use explicit Port construction.
            ports[port_name] = Port{direction, PortType::Any, Domain::Electrical, false, std::nullopt};
        }
    }

    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_,
        std::unordered_map<std::string, std::string> ports_
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        for (const auto& [port_name, dir_str] : ports_) {
            bp2::Direction dir = (dir_str == "in" || dir_str == "i" || dir_str == "input") ? bp2::Direction::Input : bp2::Direction::Output;
            ports[port_name] = Port{dir, PortType::Any, Domain::Electrical, false, std::nullopt};
        }
    }
};
