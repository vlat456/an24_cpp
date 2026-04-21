#pragma once

#include <string>

#include "core/model/component_registry.h"

ComponentRegistry load_component_registry(const std::string& library_dir = "library/");
