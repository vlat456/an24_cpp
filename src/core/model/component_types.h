#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <utility>
#include <unordered_map>

#include "core/domain_types.h"
#include "blueprint_v2/interface/direction.h"

/// Typed discriminator for solver_role.kind — replaces raw string comparison.
/// Parsed from JSON at load time; unknown values fail fast.
enum class SolverRoleKind : uint8_t {
    TheveninSource,
    ConductanceBranch,
    FixedVoltageNode,
    KnobSwitchBranches,
    PressureSource,
    FlowBranch,
    FixedPressureNode,
};

/// Parse a solver_role "kind" string into typed enum. Throws on unknown value.
inline SolverRoleKind parse_solver_role_kind(const std::string& s) {
    if (s == "TheveninSource")    return SolverRoleKind::TheveninSource;
    if (s == "ConductanceBranch") return SolverRoleKind::ConductanceBranch;
    if (s == "FixedVoltageNode")  return SolverRoleKind::FixedVoltageNode;
    if (s == "KnobSwitchBranches") return SolverRoleKind::KnobSwitchBranches;
    if (s == "PressureSource")    return SolverRoleKind::PressureSource;
    if (s == "FlowBranch")        return SolverRoleKind::FlowBranch;
    if (s == "FixedPressureNode") return SolverRoleKind::FixedPressureNode;
    throw std::runtime_error("Unknown solver_role kind '" + s + "'");
}

/// Convert SolverRoleKind back to string (for diagnostics/codegen).
inline const char* solver_role_kind_name(SolverRoleKind k) {
    switch (k) {
        case SolverRoleKind::TheveninSource:     return "TheveninSource";
        case SolverRoleKind::ConductanceBranch:  return "ConductanceBranch";
        case SolverRoleKind::FixedVoltageNode:   return "FixedVoltageNode";
        case SolverRoleKind::KnobSwitchBranches: return "KnobSwitchBranches";
        case SolverRoleKind::PressureSource:     return "PressureSource";
        case SolverRoleKind::FlowBranch:         return "FlowBranch";
        case SolverRoleKind::FixedPressureNode:  return "FixedPressureNode";
    }
    return "Unknown";
}

/// Typed discriminator for push-scheduler participation.
/// Replaces the domain-specific `scheduler_source` + `solver_owned_electrical` booleans.
///   - Source:   push-scheduler source (drives other components)
///   - Consumer: push-scheduler consumer (reacts to sources)
///   - None:     no push-scheduler role (exclusively managed by a subsolver)
enum class SchedulerRoleKind : uint8_t {
    Source,
    Consumer,
    None,
};

/// Parse a "scheduler_role" string into typed enum. Throws on unknown value.
inline SchedulerRoleKind parse_scheduler_role_kind(const std::string& s) {
    if (s == "Source")   return SchedulerRoleKind::Source;
    if (s == "Consumer") return SchedulerRoleKind::Consumer;
    if (s == "None")    return SchedulerRoleKind::None;
    throw std::runtime_error("Unknown scheduler_role '" + s + "'");
}

/// Convert SchedulerRoleKind back to string (for diagnostics/codegen).
inline const char* scheduler_role_kind_name(SchedulerRoleKind k) {
    switch (k) {
        case SchedulerRoleKind::Source:   return "Source";
        case SchedulerRoleKind::Consumer: return "Consumer";
        case SchedulerRoleKind::None:    return "None";
    }
    return "Unknown";
}

struct SolverRole {
    SolverRoleKind kind;
    Domain domain = Domain::Electrical;
    std::unordered_map<std::string, std::string> port_map;
    std::unordered_map<std::string, std::string> param_map;
    std::unordered_map<std::string, float> value_map;
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
