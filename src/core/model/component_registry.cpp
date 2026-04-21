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

MenuTree CatalogData::build_menu_tree(
    const std::unordered_map<std::string, ComponentSpec>& types,
    const PresentationRegistry& presentation) const
{
    MenuTree root;
    for (const auto& [classname, _] : types) {
        if (classname.empty()) {
            continue;
        }

        MenuTree* node = &root;
        auto cat_it = categories.find(classname);
        if (cat_it != categories.end() && !cat_it->second.empty()) {
            const std::string& cat = cat_it->second;
            size_t start = 0;
            while (start < cat.size()) {
                size_t slash = cat.find('/', start);
                std::string segment = slash == std::string::npos
                    ? cat.substr(start)
                    : cat.substr(start, slash - start);
                node = &node->children[segment];
                start = slash == std::string::npos ? cat.size() : slash + 1;
            }
        }

        node->entries.push_back(classname);
        const auto* pres = presentation.get(classname);
        node->labels[classname] = pres && !pres->description.empty()
            ? pres->description
            : classname;
    }

    std::function<void(MenuTree&)> sort_tree = [&](MenuTree& tree) {
        std::sort(tree.entries.begin(), tree.entries.end());
        for (auto& [_, child] : tree.children) {
            sort_tree(child);
        }
    };
    sort_tree(root);

    return root;
}

std::optional<std::string> ComponentRegistry::validate_instance(const DeviceInstance& instance) const {
    return validate_device_against_registry(*this, instance);
}

std::optional<std::string> ComponentRegistry::validate_instance(const ResolvedDevice& instance) const {
    return validate_device_against_registry(*this, instance);
}

std::vector<std::string> ComponentRegistry::get_composites_topo_sorted() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::set<std::string> in_stack;

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visited.count(name)) {
            return;
        }
        if (in_stack.count(name)) {
            throw std::runtime_error("Cycle in composite hierarchy: " + name);
        }

        auto it = types.find(name);
        if (it == types.end()) {
            throw std::runtime_error("Missing composite dependency: " + name);
        }
        if (is_primitive(it->second)) {
            return;
        }

        in_stack.insert(name);
        struct StackGuard {
            std::set<std::string>& in_stack;
            const std::string& name;

            ~StackGuard() {
                in_stack.erase(name);
            }
        } guard{in_stack, name};

        const auto* composite = as_composite(it->second);
        if (composite) {
            for (const auto& ref : composite->sub_blueprints) {
                visit(ref.type_name);
            }
        }

        visited.insert(name);
        result.push_back(name);
    };

    for (const auto& [name, spec] : types) {
        if (is_composite(spec)) {
            visit(name);
        }
    }

    return result;
}
