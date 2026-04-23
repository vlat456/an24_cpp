#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <utility>
#include <unordered_map>

#include "core/domain_types.h"
#include "blueprint_v2/interface/direction.h"

struct SolverRole {
    std::string kind;
    std::unordered_map<std::string, std::string> port_map;
    std::unordered_map<std::string, std::string> param_map;
    std::unordered_map<std::string, float> value_map;
};

struct ExecutionPhases {
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

enum class ParamSchemaType {
    Float,
    Int,
    Bool,
    String,
    Table,  ///< Arena-allocated breakpoint table (key:value pairs)
};

struct ParamSpec {
    ParamSchemaType type = ParamSchemaType::String;
    std::string default_value;
    std::optional<double> min;
    std::optional<double> max;
    bool required = false;
    bool visual_only = false;
    /// For Table type: component field that receives the arena offset.
    std::string arena_field_offset;
    /// For Table type: component field that receives the entry count.
    std::string arena_field_size;
    /// Component field name override. Empty means param_name == field_name.
    /// Used when the JSON param name differs from the C++ member name.
    std::string field;
};

struct BridgePortDefinition {
    std::string id;
    std::string exposed_port;
    bp2::BridgeDirection direction = bp2::BridgeDirection::Input;
    PortType type = PortType::Contextual;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    std::string label;
};

struct SubBlueprintRef {
    std::string id;
    std::string blueprint_path;
    std::string type_name;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    std::map<std::string, std::string> params_override;
};
