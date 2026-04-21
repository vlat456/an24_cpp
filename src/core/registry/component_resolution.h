#pragma once

#include "core/model/component_registry.h"
#include "core/model/resolved_device.h"

struct PresentationRegistry;

ResolvedDevice resolve_component(
    const DeviceInstance& instance,
    const ComponentSpec& definition
);

std::unordered_map<std::string, Port> extract_exposed_ports(const ComponentSpec& spec);
