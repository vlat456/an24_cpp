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
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{direction, type, domain_for_port_type(type), false, std::nullopt};
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
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{dir, type, domain_for_port_type(type), false, std::nullopt};
        }
    }
};
