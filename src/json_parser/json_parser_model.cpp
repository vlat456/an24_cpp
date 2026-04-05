#include "json_parser.h"

#include <algorithm>
#include <functional>
#include <set>

MenuTree TypeRegistry::build_menu_tree() const {
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
        auto it = types.find(classname);
        if (it != types.end() && !it->second.description.empty()) {
            node->labels[classname] = it->second.description;
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

std::optional<std::string> TypeRegistry::validate_instance(const DeviceInstance& instance) const {
    if (!has(instance.classname)) {
        return "Unknown classname '" + instance.classname + "' in device '" + instance.name + "'";
    }

    const auto* def = get(instance.classname);
    if (!def) {
        return "Type definition not found for '" + instance.classname + "'";
    }

    for (const auto& [port_name, port] : instance.ports) {
        (void)port;
        if (!def->ports.count(port_name)) {
            return "Unknown port '" + port_name + "' in device '" + instance.name +
                   "' of type '" + instance.classname + "'. Valid ports: " +
                   [&]() {
                       std::string valid_ports;
                       for (const auto& [name, _] : def->ports) {
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

TypeDefinition expand_sub_blueprint_references(
    const TypeDefinition& td,
    const TypeRegistry& registry,
    std::set<std::string>& loading_stack)
{
    if (td.cpp_class) return td;

    if (!loading_stack.insert(td.classname).second) {
        throw std::runtime_error("Circular sub-blueprint reference: " + td.classname);
    }

    TypeDefinition result = td;
    result.sub_blueprints.clear();

    for (const auto& ref : td.sub_blueprints) {
        const auto* sub_td = registry.get(ref.type_name);
        if (!sub_td) {
            throw std::runtime_error(
                "Sub-blueprint '" + ref.type_name + "' not found in TypeRegistry"
                " (referenced by '" + td.classname + "' as '" + ref.id + "')");
        }

        auto expanded = expand_sub_blueprint_references(*sub_td, registry, loading_stack);

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
    }

    loading_stack.erase(td.classname);
    return result;
}

std::vector<std::string> TypeRegistry::get_composites_topo_sorted() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::set<std::string> in_stack;

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visited.count(name)) return;
        if (in_stack.count(name))
            throw std::runtime_error("Cycle in composite hierarchy: " + name);
        in_stack.insert(name);

        auto it = types.find(name);
        if (it == types.end() || it->second.cpp_class) return;

        for (const auto& ref : it->second.sub_blueprints) {
            visit(ref.type_name);
        }

        in_stack.erase(name);
        visited.insert(name);
        result.push_back(name);
    };

    for (const auto& [name, td] : types) {
        if (!td.cpp_class) visit(name);
    }
    return result;
}
