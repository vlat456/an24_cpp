#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <utility>

#include "core/model/port.h"
#include "core/model/component_types.h"

struct ResolvedDevice {
    std::string name;
    std::string template_name;
    std::string classname;
    std::string display_name;
    std::string priority = "med";
    std::optional<size_t> bucket;
    bool critical = false;
    bool visual_only = false;
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

inline bool device_visual_only(const ResolvedDevice& dev) {
    return dev.visual_only;
}
