#pragma once

#include "core/model/component_registry.h"

inline ExecutionPhases make_execution(
    bool electrical_passive,
    bool electrical_observer,
    bool logical,
    bool control_commit,
    bool electrical_actuator,
    bool finalize,
    bool mechanical,
    bool hydraulic,
    bool thermal) {
    ExecutionPhases phases;
    phases.electrical_passive = electrical_passive;
    phases.electrical_observer = electrical_observer;
    phases.logical = logical;
    phases.control_commit = control_commit;
    phases.electrical_actuator = electrical_actuator;
    phases.finalize = finalize;
    phases.mechanical = mechanical;
    phases.hydraulic = hydraulic;
    phases.thermal = thermal;
    return phases;
}
