#include "json_parser.h"

#include <algorithm>
#include <functional>
#include <set>

MenuTree CatalogData::build_menu_tree(const std::unordered_map<std::string, ComponentSpec>& types,
                                     const PresentationRegistry& presentation) const {
    MenuTree root;
    for (const auto& [classname, _] : types) {
        if (classname.empty()) continue;
        MenuTree* node = &root;

        auto cat_it = categories.find(classname);
        if (cat_it != categories.end() && !cat_it->second.empty()) {
            const std::string& cat = cat_it->second;
            size_t start = 0;
            while (start < cat.size()) {
                size_t slash = cat.find('/', start);
                std::string segment = (slash == std::string::npos)
                    ? cat.substr(start)
                    : cat.substr(start, slash - start);
                node = &node->children[segment];
                start = (slash == std::string::npos) ? cat.size() : slash + 1;
            }
        }

        node->entries.push_back(classname);
        const auto* pres = presentation.get(classname);
        if (pres && !pres->description.empty()) {
            node->labels[classname] = pres->description;
        } else {
            node->labels[classname] = classname;
        }
    }

    std::function<void(MenuTree&)> sort_tree = [&](MenuTree& t) {
        std::sort(t.entries.begin(), t.entries.end());
        for (auto& [_, child] : t.children) {
            sort_tree(child);
        }
    };
    sort_tree(root);

    return root;
}

std::optional<std::string> ComponentRegistry::validate_instance(const DeviceInstance& instance) const {
    if (!has(instance.classname)) {
        return "Unknown classname '" + instance.classname + "' in device '" + instance.name + "'";
    }

    const auto* def = get(instance.classname);
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

    if (instance.domains.empty()) {
        return "No domains specified for device '" + instance.name + "' of type '" + instance.classname + "'";
    }

    return std::nullopt;
}

CompositeSpec expand_sub_blueprint_references(
    const CompositeSpec& td,
    const ComponentRegistry& registry,
    std::set<std::string>& loading_stack)
{
    // CompositeSpec is already composite, no cpp_class check needed

    if (!loading_stack.insert(td.classname).second) {
        throw std::runtime_error("Circular sub-blueprint reference: " + td.classname);
    }

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
                auto dot = override_key.find('.');
                if (dot == std::string::npos) continue;
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

    loading_stack.erase(td.classname);
    return result;
}

std::vector<std::string> ComponentRegistry::get_composites_topo_sorted() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::set<std::string> in_stack;

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visited.count(name)) return;
        if (in_stack.count(name))
            throw std::runtime_error("Cycle in composite hierarchy: " + name);
        in_stack.insert(name);

        auto it = types.find(name);
        if (it == types.end() || is_primitive(it->second)) return;

        const auto* composite = as_composite(it->second);
        if (composite) {
            for (const auto& ref : composite->sub_blueprints) {
                visit(ref.type_name);
            }
        }

        in_stack.erase(name);
        visited.insert(name);
        result.push_back(name);
    };

    for (const auto& [name, spec] : types) {
        if (is_composite(spec)) visit(name);
    }
    return result;
}
