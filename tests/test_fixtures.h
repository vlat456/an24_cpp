#pragma once

#include "core/model/component_registry.h"

inline BridgePortDefinition make_bridge_port_def(const std::string& id,
                                                 bp2::BridgeDirection direction,
                                                 PortType type = PortType::Any,
                                                 const std::string& exposed_port = "") {
    BridgePortDefinition bridge;
    bridge.id = id;
    bridge.exposed_port = exposed_port.empty() ? id : exposed_port;
    bridge.direction = direction;
    bridge.type = type;
    bridge.label = bridge.exposed_port;
    return bridge;
}

inline PrimitiveSpec make_indicator_light_type() {
    PrimitiveSpec td;
    td.classname = "IndicatorLight";
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.ports["brightness"] = Port{bp2::Direction::Output, PortType::I, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["conductance"] = ParamSpec{ParamSchemaType::Float, "0.002"};
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = false;
    return td;
}

inline PrimitiveSpec make_refnode_type(bp2::Direction direction = bp2::Direction::Input) {
    PrimitiveSpec td;
    td.classname = "RefNode";
    td.ports["v"] = Port{direction, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.solver.scheduler_source = true;
    td.params["value"] = ParamSpec{ParamSchemaType::Float, "0.0"};
    SolverRole role;
    role.kind = "FixedVoltageNode";
    role.port_map["node"] = "v";
    role.param_map["voltage"] = "value";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = false;
    return td;
}

inline PrimitiveSpec make_electrical_source_type() {
    PrimitiveSpec td;
    td.classname = "ElectricalSource";
    
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["voltage"] = ParamSpec{ParamSchemaType::Float, "28.0"};
    td.params["resistance"] = ParamSpec{ParamSchemaType::Float, "0.01"};
    SolverRole role;
    role.kind = "TheveninSource";
    role.port_map["pos"] = "v_out";
    role.port_map["neg"] = "v_in";
    role.param_map["voltage"] = "voltage";
    role.param_map["resistance"] = "resistance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = true;
    return td;
}

inline PrimitiveSpec make_electrical_conductance_type() {
    PrimitiveSpec td;
    td.classname = "ElectricalConductance";
    
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["conductance"] = ParamSpec{ParamSchemaType::Float, "0.1"};
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = true;
    return td;
}

inline PrimitiveSpec make_generator_type() {
    PrimitiveSpec td;
    td.classname = "Generator";
    
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["v_nominal"] = ParamSpec{ParamSchemaType::Float, "28.5"};
    td.params["internal_r"] = ParamSpec{ParamSchemaType::Float, "0.005"};
    SolverRole role;
    role.kind = "TheveninSource";
    role.port_map["pos"] = "v_out";
    role.port_map["neg"] = "v_in";
    role.param_map["voltage"] = "v_nominal";
    role.param_map["resistance"] = "internal_r";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = true;
    return td;
}

inline PrimitiveSpec make_currentsense_type() {
    PrimitiveSpec td;
    td.classname = "CurrentSense";
    
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.ports["i_out"] = Port{bp2::Direction::Output, PortType::I, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["conductance"] = ParamSpec{ParamSchemaType::Float, "0.05"};
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = false;
    return td;
}

inline PrimitiveSpec make_resistor_type() {
    PrimitiveSpec td;
    td.classname = "Resistor";
    
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_passive = true};
    td.params["conductance"] = ParamSpec{ParamSchemaType::Float, "0.1"};
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    td.solver.solver_role = role;
    td.solver.solver_owned_electrical = true;
    return td;
}

inline PrimitiveSpec make_voltmeter_type() {
    PrimitiveSpec td;
    td.classname = "Voltmeter";
    
    td.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_observer = true};
    return td;
}

inline PrimitiveSpec make_any_v_to_bool_type() {
    PrimitiveSpec td;
    td.classname = "Any_V_to_Bool";
    
    td.ports["Vin"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    td.ports["o"] = Port{bp2::Direction::Output, PortType::Bool, std::nullopt};
    td.domains = {Domain::Logical};
    td.solver.execution = {.logical = true};
    return td;
}

inline PrimitiveSpec make_value_type() {
    PrimitiveSpec td;
    td.classname = "Value";
    
    td.ports["o"] = Port{bp2::Direction::Output, PortType::Any, std::nullopt};
    td.domains = {Domain::Logical};
    td.solver.execution = {.logical = true};
    return td;
}

inline PrimitiveSpec make_bus_type() {
    PrimitiveSpec td;
    td.classname = "Bus";
    td.ports["v"] = Port{bp2::Direction::InOut, PortType::V, std::nullopt};
    td.domains = {Domain::Electrical};
    td.solver.execution = {.electrical_observer = true};
    return td;
}

// Note: visual_only type-level flag moved to TypePresentation - not set here

inline void register_lamp_composite_types(ComponentRegistry& registry) {
    registry.register_type("IndicatorLight", make_indicator_light_type());
}

inline void register_basic_electrical_types(ComponentRegistry& registry) {
    registry.register_type("ElectricalSource", make_electrical_source_type());
    registry.register_type("ElectricalConductance", make_electrical_conductance_type());
    registry.register_type("RefNode", make_refnode_type());
}

inline void register_generator_sense_ref_types(ComponentRegistry& registry) {
    registry.register_type("Generator", make_generator_type());
    registry.register_type("CurrentSense", make_currentsense_type());
    registry.register_type("RefNode", make_refnode_type());
}
