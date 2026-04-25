#pragma once

#include "core/model/port.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

/// Lightweight device descriptor used in CompositeSpec and JSON parsing.
/// Represents a concrete device instance (name, type, params, ports, layout).
/// Not to be confused with ResolvedDevice — this is the pre-resolution form.
struct DeviceInstance {
    std::string name;            ///< Instance name (unique within composite)
    std::string template_name;   ///< Source template (JSON "template" field)
    std::string classname;       ///< Component type (e.g. "Generator", "Bus")
    std::string display_name;    ///< Human-readable label (optional)
    std::string priority = "med";
    std::optional<size_t> bucket;
    bool critical = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::optional<std::pair<float,float>> pos;   ///< Editor canvas position
    std::optional<std::pair<float,float>> size;   ///< Editor canvas size

    DeviceInstance() = default;

    /// Convenience constructor — sets name and classname explicitly.
    /// Prefer this over aggregate init to avoid field-order confusion
    /// (aggregate DeviceInstance{"x", "Y"} puts "Y" in template_name, not classname).
    DeviceInstance(std::string name_, std::string classname_)
        : name(std::move(name_)), classname(std::move(classname_)) {}
};
