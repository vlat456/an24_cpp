#pragma once

#include "json_parser/json_parser.h"
#include "test_helpers.h"

inline BridgePortDefinition make_bridge_port_def(const std::string& id,
                                                 PortDirection side,
                                                 PortType type = PortType::Any,
                                                 const std::string& exposed_port = "") {
    BridgePortDefinition bridge;
    bridge.id = id;
    bridge.exposed_port = exposed_port.empty() ? id : exposed_port;
    bridge.side = side;
    bridge.type = type;
    bridge.label = bridge.exposed_port;
    return bridge;
}

inline TypeDefinition make_indicator_light_type() {
    TypeDefinition td;
    td.classname = "IndicatorLight";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["conductance"] = "0.002";
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = false;
    return td;
}

inline TypeDefinition make_refnode_type(PortDirection direction = PortDirection::In) {
    TypeDefinition td;
    td.classname = "RefNode";
    td.cpp_class = true;
    td.ports["v"] = Port{direction, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.scheduler_source = true;
    td.params["value"] = "0.0";
    SolverRole role;
    role.kind = "FixedVoltageNode";
    role.port_map["node"] = "v";
    role.param_map["voltage"] = "value";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = false;
    return td;
}

inline TypeDefinition make_electrical_source_type() {
    TypeDefinition td;
    td.classname = "ElectricalSource";
    td.cpp_class = true;
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["voltage"] = "28.0";
    td.params["resistance"] = "0.01";
    SolverRole role;
    role.kind = "TheveninSource";
    role.port_map["pos"] = "v_out";
    role.port_map["neg"] = "v_in";
    role.param_map["voltage"] = "voltage";
    role.param_map["resistance"] = "resistance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = true;
    return td;
}

inline TypeDefinition make_electrical_conductance_type() {
    TypeDefinition td;
    td.classname = "ElectricalConductance";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["conductance"] = "0.1";
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = true;
    return td;
}

inline TypeDefinition make_generator_type() {
    TypeDefinition td;
    td.classname = "Generator";
    td.cpp_class = true;
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["v_nominal"] = "28.5";
    td.params["internal_r"] = "0.005";
    SolverRole role;
    role.kind = "TheveninSource";
    role.port_map["pos"] = "v_out";
    role.port_map["neg"] = "v_in";
    role.param_map["voltage"] = "v_nominal";
    role.param_map["resistance"] = "internal_r";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = true;
    return td;
}

inline TypeDefinition make_currentsense_type() {
    TypeDefinition td;
    td.classname = "CurrentSense";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.ports["i_out"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["conductance"] = "0.05";
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    role.value_map["bind_handle"] = 1.0f;
    td.solver_role = role;
    td.solver_owned_electrical = false;
    return td;
}

inline TypeDefinition make_resistor_type() {
    TypeDefinition td;
    td.classname = "Resistor";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, false, false, false, false, false, false, false);
    td.params["conductance"] = "0.1";
    SolverRole role;
    role.kind = "ConductanceBranch";
    role.port_map["a"] = "v_in";
    role.port_map["b"] = "v_out";
    role.param_map["g"] = "conductance";
    td.solver_role = role;
    td.solver_owned_electrical = true;
    return td;
}

inline TypeDefinition make_voltmeter_type() {
    TypeDefinition td;
    td.classname = "Voltmeter";
    td.cpp_class = true;
    td.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(false, true, false, false, false, false, false, false, false);
    return td;
}

inline TypeDefinition make_any_v_to_bool_type() {
    TypeDefinition td;
    td.classname = "Any_V_to_Bool";
    td.cpp_class = true;
    td.ports["Vin"] = Port{PortDirection::In, PortType::V, std::nullopt};
    td.ports["o"] = Port{PortDirection::Out, PortType::Bool, std::nullopt};
    td.domains = {{Domain::Logical}};
    td.execution = make_execution(false, false, true, false, false, false, false, false, false);
    return td;
}

inline TypeDefinition make_value_type() {
    TypeDefinition td;
    td.classname = "Value";
    td.cpp_class = true;
    td.ports["o"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    td.domains = {{Domain::Logical}};
    td.execution = make_execution(false, true, false, false, false, false, false, false, false);
    return td;
}

inline void register_lamp_composite_types(TypeRegistry& registry) {
    registry.types["IndicatorLight"] = make_indicator_light_type();
}

inline void register_basic_electrical_types(TypeRegistry& registry) {
    registry.types["ElectricalSource"] = make_electrical_source_type();
    registry.types["ElectricalConductance"] = make_electrical_conductance_type();
    registry.types["RefNode"] = make_refnode_type();
}

inline void register_generator_sense_ref_types(TypeRegistry& registry) {
    registry.types["Generator"] = make_generator_type();
    registry.types["CurrentSense"] = make_currentsense_type();
    registry.types["RefNode"] = make_refnode_type();
}
