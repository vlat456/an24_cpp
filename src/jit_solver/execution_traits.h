#pragma once

#include "../json_parser/json_parser.h"
#include <string>

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
    bool electrical_noop = false;
};

inline Domain get_device_domain_mask(const DeviceInstance& dev) {
    if (!dev.domains.empty()) {
        Domain mask = static_cast<Domain>(0);
        for (Domain d : dev.domains) {
            mask = mask | d;
        }
        return mask;
    }

    auto it = dev.params.find("domain");
    if (it != dev.params.end()) {
        const std::string& s = it->second;
        Domain mask = static_cast<Domain>(0);
        if (s.find("Electrical") != std::string::npos) mask = mask | Domain::Electrical;
        if (s.find("Logical") != std::string::npos) mask = mask | Domain::Logical;
        if (s.find("Mechanical") != std::string::npos) mask = mask | Domain::Mechanical;
        if (s.find("Hydraulic") != std::string::npos) mask = mask | Domain::Hydraulic;
        if (s.find("Thermal") != std::string::npos) mask = mask | Domain::Thermal;
        if (static_cast<uint8_t>(mask) != 0) return mask;
    }

    return Domain::Electrical;
}

inline ExecutionTraits get_strict_execution_traits(const std::string& classname, Domain domain_mask) {
    ExecutionTraits t{};

    if (has_domain(domain_mask, Domain::Electrical)) t.electrical_passive = true;
    if (has_domain(domain_mask, Domain::Logical)) t.logical = true;
    if (has_domain(domain_mask, Domain::Mechanical)) t.mechanical = true;
    if (has_domain(domain_mask, Domain::Hydraulic)) t.hydraulic = true;
    if (has_domain(domain_mask, Domain::Thermal)) t.thermal = true;

    t.electrical_noop = (classname == "Bus" || classname == "Voltmeter");

    // Explicit per-class mapping (strict, no fuzzy inference).
    if (classname == "VoltageSense" || classname == "Voltmeter") {
        t.electrical_passive = false;
        t.electrical_observer = true;
    }

    if (classname == "CurrentSense") {
        t.electrical_passive = true;
        t.electrical_observer = true;
    }

    if (classname == "Bus") {
        t.electrical_passive = false;
    }

    if (classname == "ControlledVoltageSource" ||
        classname == "ControlledCurrentSource" ||
        classname == "VariableConductance") {
        t.electrical_passive = false;
        t.electrical_actuator = true;
    }

    if (classname == "PID" || classname == "PD" || classname == "PI" || classname == "P") {
        t.electrical_passive = true;
        t.logical = true;
    }

    if (classname == "DMR400" || classname == "RU19A" || classname == "GS24" ||
        classname == "RUG82" || classname == "GidroAccumulator" ||
        classname == "FuelTank" || classname == "LerpNode") {
        t.finalize = true;
    }

    if (classname == "HoldButton" || classname == "Switch" || classname == "Relay" || classname == "AZS") {
        t.control_commit = true;
    }

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
    if (t.electrical_noop) result += "ElecNoop ";
    if (result.empty()) return "None";
    if (result.back() == ' ') result.pop_back();
    return result;
}
