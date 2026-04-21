#pragma once

#include <set>

#include "core/model/component_registry.h"

CompositeSpec expand_sub_blueprint_references(
    const CompositeSpec& td,
    const ComponentRegistry& registry,
    std::set<std::string>& loading_stack);
