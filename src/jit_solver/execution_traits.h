#pragma once

#include "../json_parser/json_parser.h"
#include <string>
#include <stdexcept>

struct DeviceInstance;
struct ExecutionPhases;

struct ExecutionTraits {
    bool electrical_passive = false;
    bool electrical_observer = false;
    bool logical = false;
    bool control_commit = false;
    bool electrical_actuator = false;
    bool finalize = false;
    bool mechanical = false;
    bool hydraulic = false;
    bool thermal = false;
};

inline ExecutionTraits get_execution_traits(const DeviceInstance& dev) {
    if (!dev.execution.has_value()) {
        throw std::runtime_error("Missing execution metadata for component: " + dev.classname);
    }

    const ExecutionPhases& ep = dev.execution.value();
    ExecutionTraits t{};

    t.electrical_passive = ep.electrical_passive;
    t.electrical_observer = ep.electrical_observer;
    t.logical = ep.logical;
    t.control_commit = ep.control_commit;
    t.electrical_actuator = ep.electrical_actuator;
    t.finalize = ep.finalize;
    t.mechanical = ep.mechanical;
    t.hydraulic = ep.hydraulic;
    t.thermal = ep.thermal;

    return t;
}

inline std::string get_execution_traits_string(const ExecutionTraits& t) {
    std::string result;
    if (t.electrical_passive) result += "ElecPassive ";
    if (t.electrical_observer) result += "ElecObserver ";
    if (t.logical) result += "Logical ";
    if (t.control_commit) result += "ControlCommit ";
    if (t.electrical_actuator) result += "ElecActuator ";
    if (t.finalize) result += "Finalize ";
    if (t.mechanical) result += "Mechanical ";
    if (t.hydraulic) result += "Hydraulic ";
    if (t.thermal) result += "Thermal ";
    if (result.empty()) return "None";
    if (result.back() == ' ') result.pop_back();
    return result;
}
