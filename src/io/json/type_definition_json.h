#pragma once

#include <nlohmann/json.hpp>

#include "core/model/component_registry.h"

std::pair<ComponentSpec, TypePresentation> parse_type_definition(const nlohmann::json& j);
