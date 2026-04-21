#include "core/registry/composite_expansion.h"

CompositeSpec expand_sub_blueprint_references(
    const CompositeSpec& td,
    const ComponentRegistry& registry,
    std::set<std::string>& loading_stack)
{
    if (!loading_stack.insert(td.classname).second) {
        throw std::runtime_error("Circular sub-blueprint reference: " + td.classname);
    }

    struct LoadingStackGuard {
        std::set<std::string>& loading_stack;
        const std::string& classname;

        ~LoadingStackGuard() {
            loading_stack.erase(classname);
        }
    } guard{loading_stack, td.classname};

    CompositeSpec result = td;
    result.sub_blueprints.clear();

    for (const auto& ref : td.sub_blueprints) {
        const auto* sub_td = registry.get(ref.type_name);
        if (!sub_td) {
            throw std::runtime_error(
                "Sub-blueprint '" + ref.type_name + "' not found in ComponentRegistry"
                " (referenced by '" + td.classname + "' as '" + ref.id + "')");
        }

        const auto* sub_composite = as_composite(*sub_td);
        if (!sub_composite) {
            throw std::runtime_error(
                "Sub-blueprint '" + ref.type_name + "' is not a composite (referenced by '" + td.classname + "')");
        }

        auto expanded = expand_sub_blueprint_references(*sub_composite, registry, loading_stack);

        for (auto& dev : expanded.devices) {
            dev.name = ref.id + ":" + dev.name;

            for (const auto& [override_key, override_val] : ref.params_override) {
                const auto dot = override_key.find('.');
                if (dot == std::string::npos) {
                    continue;
                }

                std::string dev_name = override_key.substr(0, dot);
                std::string param_name = override_key.substr(dot + 1);
                std::string unprefixed = dev.name.substr(ref.id.size() + 1);
                if (unprefixed == dev_name) {
                    dev.params[param_name] = override_val;
                }
            }

            result.devices.push_back(std::move(dev));
        }

        for (auto& conn : expanded.connections) {
            conn.from = ref.id + ":" + conn.from;
            conn.to = ref.id + ":" + conn.to;
            result.connections.push_back(std::move(conn));
        }

        for (auto bridge : expanded.bridge_ports) {
            bridge.id = ref.id + ":" + bridge.id;
            result.bridge_ports.push_back(std::move(bridge));
        }
    }

    return result;
}
