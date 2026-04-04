#pragma once

/// Shared test helper: canonical ExecutionPhases for each component class.
/// Values must match the corresponding .blueprint files in library/.

#include "json_parser/json_parser.h"

namespace test_exec {

// ---- Pure electrical passive ----
// RefNode, Battery, Resistor, Load, IndicatorLight (Lamp)
inline ExecutionPhases electrical_passive() {
    ExecutionPhases p;
    p.electrical_passive = true;
    return p;
}

// ---- Bus: all false (no-op junction) ----
inline ExecutionPhases bus() {
    return ExecutionPhases{};
}

// ---- BlueprintInput / BlueprintOutput ----
inline ExecutionPhases blueprint_port() {
    ExecutionPhases p;
    p.electrical_passive = true;
    p.logical = true;
    p.mechanical = true;
    p.hydraulic = true;
    p.thermal = true;
    return p;
}

// ---- LUT: logical only ----
inline ExecutionPhases lut() {
    ExecutionPhases p;
    p.logical = true;
    return p;
}

// ---- Switch: electrical_passive + control_commit ----
inline ExecutionPhases switch_exec() {
    ExecutionPhases p;
    p.electrical_passive = true;
    p.control_commit = true;
    return p;
}

// ---- GidroAccumulator: finalize + hydraulic ----
inline ExecutionPhases gidro_accumulator() {
    ExecutionPhases p;
    p.finalize = true;
    p.hydraulic = true;
    return p;
}

// ---- FuelTank: finalize + hydraulic ----
inline ExecutionPhases fuel_tank() {
    ExecutionPhases p;
    p.finalize = true;
    p.hydraulic = true;
    return p;
}

// ---- VoltageSense: electrical_observer + logical ----
inline ExecutionPhases voltage_sense() {
    ExecutionPhases p;
    p.electrical_observer = true;
    p.logical = true;
    return p;
}

// ---- CurrentSense: electrical_passive + electrical_observer ----
inline ExecutionPhases current_sense() {
    ExecutionPhases p;
    p.electrical_passive = true;
    p.electrical_observer = true;
    return p;
}

// ---- Controlled sources: electrical_actuator only ----
inline ExecutionPhases actuator() {
    ExecutionPhases p;
    p.electrical_actuator = true;
    return p;
}

/// Lookup by classname. Returns canonical ExecutionPhases for known cpp_class
/// components. Throws for unknown classnames — intentionally strict.
inline ExecutionPhases for_class(const std::string& classname) {
    if (classname == "RefNode")              return electrical_passive();
    if (classname == "Battery")              return electrical_passive();
    if (classname == "Resistor")             return electrical_passive();
    if (classname == "Load")                 return electrical_passive();
    if (classname == "IndicatorLight")        return electrical_passive();
    if (classname == "Lamp")                 return electrical_passive();  // alias
    if (classname == "Bus")                  return bus();
    if (classname == "BlueprintInput")        return blueprint_port();
    if (classname == "BlueprintOutput")       return blueprint_port();
    if (classname == "LUT")                  return lut();
    if (classname == "Switch")               return switch_exec();
    if (classname == "GidroAccumulator")     return gidro_accumulator();
    if (classname == "FuelTank")             return fuel_tank();
    if (classname == "VoltageSense")         return voltage_sense();
    if (classname == "CurrentSense")         return current_sense();
    if (classname == "ControlledVoltageSource") return actuator();
    if (classname == "ControlledCurrentSource") return actuator();
    throw std::runtime_error("test_exec::for_class: unknown classname: " + classname);
}

}  // namespace test_exec
