#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/model/component_registry.h"
#include "core/model/connection.h"
#include "core/model/resolved_device.h"

struct SubsystemCall {
    std::string name;
    std::string template_name;
    std::unordered_map<std::string, std::string> port_map;
};

struct SystemTemplate {
    std::string name;
    std::vector<DeviceInstance> devices;
    std::vector<SubsystemCall> subsystems;
    std::unordered_map<std::string, std::string> exposed_ports;
    std::vector<Domain> domains;
};

struct ParserContext {
    ComponentRegistry registry;
    std::unordered_map<std::string, SystemTemplate> templates;
    std::vector<ResolvedDevice> devices;
    std::vector<Connection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    std::unordered_map<std::string, float> initial_values;

    const ResolvedDevice* find_device(const std::string& name) const {
        for (const auto& dev : devices) {
            if (dev.name == name) {
                return &dev;
            }
        }
        return nullptr;
    }

    const SystemTemplate* get_template(const std::string& name) const {
        auto it = templates.find(name);
        if (it != templates.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

ParserContext parse_json(const std::string& json_text);
ParserContext parse_json(const std::string& json_text, const std::string& library_dir);

std::string serialize_json(const ParserContext& ctx);
