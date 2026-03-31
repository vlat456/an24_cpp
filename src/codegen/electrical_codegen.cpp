#include "codegen.h"
#include "codegen_utils.h"
#include "../parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {

float parse_float_codegen(const std::string& value, float default_val) {
    if (value.empty()) {
        return default_val;
    }
    return locale_safe::parse_float_or(value, default_val);
}

float read_param_or(const DeviceInstance& dev, const char* key, float default_val) {
    auto it = dev.params.find(key);
    if (it == dev.params.end()) {
        return default_val;
    }
    return parse_float_codegen(it->second, default_val);
}

struct ClassnameElectricalRule {
    const char* classname;
    ElectricalElementKindCodegen kind;
    const char* port_a;
    const char* port_b;
    const char* param_a;
    float param_a_default;
    const char* param_b;
    float param_b_default;
};

constexpr ClassnameElectricalRule k_classname_rules[] = {
    {"Battery",               ElectricalElementKindCodegen::TheveninSource,   "v_out", "v_in",  "v_nominal",   28.0f, "internal_r", 0.01f},
    {"Generator",             ElectricalElementKindCodegen::TheveninSource,   "v_out", "v_in",  "v_nominal",   28.5f, "internal_r", 0.005f},
    {"RefNode",               ElectricalElementKindCodegen::FixedVoltageNode, "v",     nullptr, "value",        0.0f,  nullptr,      0.0f},
    {"Resistor",              ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  0.1f,  nullptr,      0.0f},
    {"IndicatorLight",        ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  1.0f,  nullptr,      0.0f},
    {"CurrentSense",          ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  1000.0f,nullptr,     0.0f},
    {"ElectricalConductance", ElectricalElementKindCodegen::ConductanceBranch,"v_in",  "v_out", "conductance",  0.1f,  nullptr,      0.0f},
    {"ElectricalSource",      ElectricalElementKindCodegen::TheveninSource,   "v_out", "v_in",  "voltage",      28.0f, "resistance", 0.01f},
};

} // anonymous namespace

ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<DeviceInstance>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options
) {
    ElectricalPlanCodegen plan;

    struct RawElement {
        ElectricalElementKindCodegen kind;
        uint32_t node_a;
        uint32_t node_b;
        float value_a;
        float value_b;
        size_t component_index;
        std::string device_name;   // for handle assignment back to wrapper components
        std::string device_classname;
    };

    auto kind_to_role = [](ElectricalElementKindCodegen k) -> std::string {
        if (k == ElectricalElementKindCodegen::FixedVoltageNode) {
            return "FixedVoltageNode";
        }
        if (k == ElectricalElementKindCodegen::TheveninSource) {
            return "TheveninSource";
        }
        return "ConductanceBranch";
    };

    std::vector<RawElement> raw_elements;

    auto device_has_any_ports = [&](const DeviceInstance& dev) -> bool {
        for (const auto& [port_name, _port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
            if (port_to_signal.find(full_port) != port_to_signal.end()) {
                return true;
            }
        }
        return false;
    };

    auto resolve_port = [&](const DeviceInstance& dev, const std::string& port_name) -> std::optional<uint32_t> {
        const std::string full_port = dev.name + "." + port_name;
        auto it = port_to_signal.find(full_port);
        if (it == port_to_signal.end()) {
            if (options.strict_port_resolution) {
                throw std::runtime_error(
                    "[codegen] electrical port '" + full_port +
                    "' not found in signal map for device '" + dev.name +
                    "' (classname: " + dev.classname + ")"
                );
            }
            if (options.warn_on_missing_ports) {
                spdlog::warn("[codegen] electrical port '{}' not found in signal map for device '{}' (classname: {}). Element skipped from electrical plan.",
                    full_port, dev.name, dev.classname);
            }
            return std::nullopt;
        }
        return it->second;
    };

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        if (!device_has_any_ports(dev)) {
            continue;
        }

        if (dev.solver_role.has_value()) {
            const auto& role = *dev.solver_role;

            if (role.kind == "FixedVoltageNode") {
                float value = 0.0f;
                auto it_val = role.param_map.find("voltage");
                if (it_val != role.param_map.end()) {
                    auto it_param = dev.params.find(it_val->second);
                    if (it_param != dev.params.end()) {
                        value = parse_float_codegen(it_param->second, 0.0f);
                    }
                }
                auto node_a_opt = resolve_port(dev, role.port_map.at("node"));
                if (!node_a_opt.has_value()) {
                    continue;
                }
                raw_elements.push_back({
                    ElectricalElementKindCodegen::FixedVoltageNode,
                    *node_a_opt, UINT32_MAX, value, 0.0f, element_idx++,
                    dev.name, dev.classname
                });
            } else if (role.kind == "TheveninSource") {
                float voltage = 28.0f;
                float resistance = 0.01f;
                auto it_v = role.param_map.find("voltage");
                if (it_v != role.param_map.end()) {
                    auto it_param = dev.params.find(it_v->second);
                    if (it_param != dev.params.end()) {
                        voltage = parse_float_codegen(it_param->second, 28.0f);
                    }
                }
                auto it_r = role.param_map.find("resistance");
                if (it_r != role.param_map.end()) {
                    auto it_param = dev.params.find(it_r->second);
                    if (it_param != dev.params.end()) {
                        resistance = parse_float_codegen(it_param->second, 0.01f);
                    }
                }
                auto node_pos_opt = resolve_port(dev, role.port_map.at("pos"));
                auto node_neg_opt = resolve_port(dev, role.port_map.at("neg"));
                if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) {
                    continue;
                }
                raw_elements.push_back({
                    ElectricalElementKindCodegen::TheveninSource,
                    *node_pos_opt, *node_neg_opt, voltage, resistance, element_idx++,
                    dev.name, dev.classname
                });
            } else if (role.kind == "ConductanceBranch") {
                float conductance = 0.1f;
                auto it_g = role.param_map.find("g");
                if (it_g != role.param_map.end()) {
                    auto it_param = dev.params.find(it_g->second);
                    if (it_param != dev.params.end()) {
                        conductance = parse_float_codegen(it_param->second, 0.1f);
                    }
                }
                auto node_a_opt = resolve_port(dev, role.port_map.at("a"));
                auto node_b_opt = resolve_port(dev, role.port_map.at("b"));
                if (!node_a_opt.has_value() || !node_b_opt.has_value()) {
                    continue;
                }
                raw_elements.push_back({
                    ElectricalElementKindCodegen::ConductanceBranch,
                    *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++,
                    dev.name, dev.classname
                });
            }
            continue;
        }

        for (const auto& rule : k_classname_rules) {
            if (dev.classname != rule.classname) {
                continue;
            }

            auto node_a_opt = resolve_port(dev, rule.port_a);
            if (!node_a_opt.has_value()) {
                break;
            }

            if (rule.kind == ElectricalElementKindCodegen::FixedVoltageNode) {
                raw_elements.push_back({
                    rule.kind,
                    *node_a_opt,
                    UINT32_MAX,
                    read_param_or(dev, rule.param_a, rule.param_a_default),
                    0.0f,
                    element_idx++,
                    dev.name, dev.classname
                });
                break;
            }

            auto node_b_opt = resolve_port(dev, rule.port_b);
            if (!node_b_opt.has_value()) {
                break;
            }

            float value_a = read_param_or(dev, rule.param_a, rule.param_a_default);
            float value_b = 0.0f;
            if (rule.param_b != nullptr) {
                value_b = read_param_or(dev, rule.param_b, rule.param_b_default);
            }

            raw_elements.push_back({
                rule.kind,
                *node_a_opt,
                *node_b_opt,
                value_a,
                value_b,
                element_idx++,
                dev.name, dev.classname
            });
            break;
        }
    }

    if (raw_elements.empty()) {
        return plan;
    }

    std::unordered_map<uint32_t, uint32_t> uf_parent;
    std::unordered_map<uint32_t, uint32_t> uf_rank;
    std::unordered_set<uint32_t> all_nodes;
    for (const auto& elem : raw_elements) {
        all_nodes.insert(elem.node_a);
        if (elem.node_b != UINT32_MAX) {
            all_nodes.insert(elem.node_b);
        }
    }
    for (uint32_t n : all_nodes) {
        uf_parent[n] = n;
        uf_rank[n] = 0;
    }

    auto uf_find = [&](uint32_t x) {
        while (uf_parent[x] != x) {
            uf_parent[x] = uf_parent[uf_parent[x]];
            x = uf_parent[x];
        }
        return x;
    };

    auto uf_unite = [&](uint32_t a, uint32_t b) {
        uint32_t ra = uf_find(a);
        uint32_t rb = uf_find(b);
        if (ra == rb) {
            return;
        }
        if (uf_rank[ra] < uf_rank[rb]) {
            std::swap(ra, rb);
        }
        uf_parent[rb] = ra;
        if (uf_rank[ra] == uf_rank[rb]) {
            uf_rank[ra]++;
        }
    };

    for (const auto& elem : raw_elements) {
        if (elem.node_b != UINT32_MAX) {
            uf_unite(elem.node_a, elem.node_b);
        }
    }

    std::map<uint32_t, std::vector<size_t>> island_members;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        uint32_t root = uf_find(raw_elements[i].node_a);
        island_members[root].push_back(i);
    }

    std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
        island_members.begin(), island_members.end());
    std::sort(sorted_islands.begin(), sorted_islands.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [root, elem_indices] : sorted_islands) {
        (void)root;
        ElectricalIslandPlanCodegen island;

        std::set<uint32_t> island_nodes;
        for (size_t idx : elem_indices) {
            island_nodes.insert(raw_elements[idx].node_a);
            if (raw_elements[idx].node_b != UINT32_MAX) {
                island_nodes.insert(raw_elements[idx].node_b);
            }
        }
        island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

        std::vector<size_t> sorted_indices = elem_indices;
        std::sort(sorted_indices.begin(), sorted_indices.end());
        for (size_t idx : sorted_indices) {
            const auto& re = raw_elements[idx];
            island.elements.push_back({
                re.kind,
                re.node_a,
                re.node_b,
                re.value_a,
                re.value_b,
                static_cast<uint32_t>(re.component_index)
            });
        }

        plan.islands.push_back(std::move(island));
    }

    // Build stable symbolic binding list for wrapper components.
    // Use device_name/classname stored on raw elements (not devices array index,
    // since component_index != device array index when non-electrical devices exist).
    std::unordered_map<uint32_t, size_t> component_to_raw_idx;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        component_to_raw_idx[static_cast<uint32_t>(raw_elements[i].component_index)] = i;
    }

    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> component_to_island_elem;
    for (size_t island_i = 0; island_i < plan.islands.size(); ++island_i) {
        const auto& island = plan.islands[island_i];
        for (size_t elem_i = 0; elem_i < island.elements.size(); ++elem_i) {
            const auto& elem = island.elements[elem_i];
            component_to_island_elem[elem.component_index] = {
                static_cast<uint32_t>(island_i),
                static_cast<uint32_t>(elem_i)
            };
        }
    }

    std::vector<ElectricalPlanCodegen::DeviceBinding> bindings;
    bindings.reserve(component_to_island_elem.size());
    for (const auto& [component_index, pos] : component_to_island_elem) {
        auto raw_it = component_to_raw_idx.find(component_index);
        if (raw_it == component_to_raw_idx.end()) {
            continue;
        }
        const auto& re = raw_elements[raw_it->second];
        if (re.device_classname == "Battery" || re.device_classname == "Generator" ||
            re.device_classname == "IndicatorLight" || re.device_classname == "CurrentSense") {
            bindings.push_back({
                sanitize_codegen_name(re.device_name),
                pos.first,
                pos.second,
                component_index
            });
        }
    }
    std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
        if (a.component_index != b.component_index) {
            return a.component_index < b.component_index;
        }
        if (a.island_index != b.island_index) {
            return a.island_index < b.island_index;
        }
        if (a.element_index != b.element_index) {
            return a.element_index < b.element_index;
        }
        return a.device_field_name < b.device_field_name;
    });
    bindings.erase(std::unique(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
        return a.device_field_name == b.device_field_name &&
               a.island_index == b.island_index &&
               a.element_index == b.element_index &&
               a.component_index == b.component_index;
    }), bindings.end());
    plan.device_bindings = std::move(bindings);

    std::vector<ElectricalPlanCodegen::ComponentDebug> debug;
    debug.reserve(raw_elements.size());
    for (size_t island_i = 0; island_i < plan.islands.size(); ++island_i) {
        const auto& island = plan.islands[island_i];
        for (size_t elem_i = 0; elem_i < island.elements.size(); ++elem_i) {
            const auto& elem = island.elements[elem_i];
            auto raw_it = component_to_raw_idx.find(elem.component_index);
            if (raw_it == component_to_raw_idx.end()) {
                continue;
            }
            const auto& re = raw_elements[raw_it->second];
            debug.push_back({
                static_cast<uint32_t>(re.component_index),
                static_cast<uint32_t>(island_i),
                static_cast<uint32_t>(elem_i),
                re.device_name,
                re.device_classname,
                kind_to_role(re.kind),
                re.node_a,
                re.node_b
            });
        }
    }
    std::sort(debug.begin(), debug.end(), [](const auto& a, const auto& b) {
        if (a.component_index != b.component_index) {
            return a.component_index < b.component_index;
        }
        if (a.island_index != b.island_index) {
            return a.island_index < b.island_index;
        }
        if (a.element_index != b.element_index) {
            return a.element_index < b.element_index;
        }
        return a.device_name < b.device_name;
    });
    plan.component_debug = std::move(debug);

    return plan;
}
