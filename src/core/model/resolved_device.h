#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <utility>

#include "core/model/port.h"
#include "core/model/component_types.h"
#include "core/model/component_kind.h"

struct ResolvedDevice {
    std::string name;
    std::string template_name;
    std::string classname;
    std::string display_name;
    ComponentKind kind = ComponentKind::Unknown; // Unknown until resolved at elaboration time
    std::string priority = "med";
    std::optional<size_t> bucket;
    bool critical = false;
    bool scheduler_source = false;
    bool solver_owned_electrical = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;
    std::optional<ExecutionPhases> execution;
    std::optional<SolverRole> solver_role;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
};
