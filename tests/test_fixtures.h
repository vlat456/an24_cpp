#pragma once

#include "json_parser/json_parser.h"
#include "test_helpers.h"

inline TypeDefinition make_blueprint_input_type() {
    TypeDefinition td;
    td.classname = "BlueprintInput";
    td.cpp_class = true;
    td.ports["port"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    td.ports["ext"] = Port{PortDirection::In, PortType::Any, std::string("port")};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, true, false, false, false, true, true, true);
    return td;
}

inline TypeDefinition make_blueprint_output_type() {
    TypeDefinition td;
    td.classname = "BlueprintOutput";
    td.cpp_class = true;
    td.ports["port"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    td.ports["ext"] = Port{PortDirection::Out, PortType::Any, std::string("port")};
    td.domains = {{Domain::Electrical}};
    td.execution = make_execution(true, false, true, false, false, false, true, true, true);
    return td;
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
    return td;
}

inline void register_lamp_composite_types(TypeRegistry& registry) {
    registry.types["BlueprintInput"] = make_blueprint_input_type();
    registry.types["BlueprintOutput"] = make_blueprint_output_type();
    registry.types["IndicatorLight"] = make_indicator_light_type();
}

inline void register_basic_electrical_types(TypeRegistry& registry) {
    registry.types["ElectricalSource"] = make_electrical_source_type();
    registry.types["ElectricalConductance"] = make_electrical_conductance_type();
    registry.types["RefNode"] = make_refnode_type();
}
