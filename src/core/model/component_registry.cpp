#include "core/model/component_registry.h"

#include <algorithm>
#include <functional>
#include <set>

namespace {

template <typename DeviceT>
std::optional<std::string> validate_device_against_registry(
    const ComponentRegistry& registry,
    const DeviceT& instance)
{
    if (!registry.has(instance.classname)) {
        return "Unknown classname '" + instance.classname + "' in device '" + instance.name + "'";
    }

    const auto* def = registry.get(instance.classname);
    if (!def) {
        return "Type definition not found for '" + instance.classname + "'";
    }

    const auto& ports = spec_ports(*def);
    for (const auto& [port_name, port] : instance.ports) {
        (void)port;
        if (!ports.count(port_name)) {
            return "Unknown port '" + port_name + "' in device '" + instance.name +
                   "' of type '" + instance.classname + "'. Valid ports: " +
                   [&]() {
                       std::string valid_ports;
                       for (const auto& [name, _] : ports) {
                           if (!valid_ports.empty()) valid_ports += ", ";
                           valid_ports += name;
                       }
                       return valid_ports;
                   }();
        }
    }

    if (spec_domains(*def).empty()) {
        return "No domains specified for device '" + instance.name + "' of type '" + instance.classname + "'";
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> ComponentRegistry::validate_instance(const DeviceInstance& instance) const {
    return validate_device_against_registry(*this, instance);
}

std::optional<std::string> ComponentRegistry::validate_instance(const ResolvedDevice& instance) const {
    return validate_device_against_registry(*this, instance);
}

template <typename F>
struct Finally {
    F f;
    ~Finally() { f(); }
};
std::vector<std::string> ComponentRegistry::get_composites_topo_sorted() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::set<std::string> in_stack;

    auto visit = [&](const std::string& name, auto&& self_ref) -> void {
        if (visited.contains(name)) return;

        if (in_stack.contains(name)) {
            throw std::runtime_error("Cycle in composite hierarchy: " + name);
        }

        const auto it = types_.find(name);
        if (it == types_.end()) {
            throw std::runtime_error("Missing composite dependency: " + name);
        }

        if (is_primitive(it->second)) return;

        in_stack.insert(name);

        auto guard = Finally([&] { in_stack.erase(name); });

        if (const auto* composite = as_composite(it->second)) {
            for (const auto& ref : composite->sub_blueprints) {
                self_ref(ref.type_name, self_ref);
            }
        }

        visited.insert(name);
        result.push_back(name);
    };

    for (const auto& [name, spec] : types_) {
        if (is_composite(spec)) {
            visit(name, visit);
        }
    }

    return result;
}
