#include "jit_solver_internal.h"
#include <algorithm>
#include <map>
#include <queue>
#include <unordered_set>
#include "../../../parse_number.h"

namespace jit_solver_impl {

void build_electrical_islands(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices)
{
    // == Batch 2: Electrical Island Extraction ==
    // Extract electrical primitive elements from supported components and partition
    // into connected islands.

    struct RawElement {
        ElectricalElementKind kind;
        uint32_t node_a;
        uint32_t node_b;
        float value_a;
        float value_b;
        size_t component_index;
        std::string device_name;
    };

    std::vector<RawElement> raw_elements;
    raw_elements.reserve(devices.size());

    // Helper to resolve port to signal index with fail-fast on missing mapping
    auto resolve_port = [&](const DeviceInstance& dev, const std::string& port_name) -> uint32_t {
        const std::string full_port = dev.name + "." + port_name;
        auto it = result.port_to_signal.find(full_port);
        if (it == result.port_to_signal.end()) {
            throw std::runtime_error("Missing required port mapping '" + full_port +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }
        return it->second;
    };

    // Helper to read a single float param by name, with default.
    auto read_param_float = [](const DeviceInstance& dev, const std::string& key, float default_val) -> float {
        auto it = dev.params.find(key);
        if (it != dev.params.end()) {
            return locale_safe::parse_float_or(it->second, default_val);
        }
        return default_val;
    };

    // Helper to resolve a solver_role port key to signal index
    auto resolve_role_port = [&](const DeviceInstance& dev, const SolverRole& role,
                                  const std::string& role_key) -> uint32_t {
        auto it = role.port_map.find(role_key);
        if (it == role.port_map.end()) {
            throw std::runtime_error("solver_role missing required port key '" + role_key +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }
        return resolve_port(dev, it->second);
    };

    // Helper to read a solver_role param by role key
    auto read_role_param = [&](const DeviceInstance& dev, const SolverRole& role,
                                const std::string& role_key, float default_val) -> float {
        auto it = role.param_map.find(role_key);
        if (it == role.param_map.end()) {
            throw std::runtime_error("solver_role missing required param key '" + role_key +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }
        return read_param_float(dev, it->second, default_val);
    };

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        // == Path 1: Metadata-driven extraction ==
        if (dev.solver_role.has_value()) {
            const auto& role = *dev.solver_role;

            if (role.kind == "FixedVoltageNode") {
                float value = read_role_param(dev, role, "voltage", 0.0f);
                uint32_t node_a = resolve_role_port(dev, role, "node");
                raw_elements.push_back({
                    ElectricalElementKind::FixedVoltageNode,
                    node_a,
                    UINT32_MAX,
                    value,
                    0.0f,
                    element_idx++,
                    dev.name
                });
            }
            else if (role.kind == "TheveninSource") {
                float voltage = read_role_param(dev, role, "voltage", 28.0f);
                float resistance = read_role_param(dev, role, "resistance", 0.01f);
                uint32_t node_pos = resolve_role_port(dev, role, "pos");
                uint32_t node_neg = resolve_role_port(dev, role, "neg");
                raw_elements.push_back({
                    ElectricalElementKind::TheveninSource,
                    node_pos,
                    node_neg,
                    voltage,
                    resistance,
                    element_idx++,
                    {}
                });
            }
            else if (role.kind == "ConductanceBranch") {
                float conductance = read_role_param(dev, role, "g", 0.1f);
                uint32_t node_a = resolve_role_port(dev, role, "a");
                uint32_t node_b = resolve_role_port(dev, role, "b");
                raw_elements.push_back({
                    ElectricalElementKind::ConductanceBranch,
                    node_a,
                    node_b,
                    conductance,
                    0.0f,
                    element_idx++,
                    {}
                });
            }
            continue;  // Metadata handled; skip classname fallback
        }

        // == Path 2: Classname-based fallback for wrapper components ==
        if (dev.classname == "Generator") {
            float v_nominal = read_param_float(dev, "v_nominal", 28.5f);
            float internal_r = read_param_float(dev, "internal_r", 0.005f);
            uint32_t node_pos = resolve_port(dev, "v_out");
            uint32_t node_neg = resolve_port(dev, "v_in");
            raw_elements.push_back({
                ElectricalElementKind::TheveninSource,
                node_pos,
                node_neg,
                v_nominal,
                internal_r,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "Resistor") {
            float conductance = read_param_float(dev, "conductance", 0.1f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                conductance,
                0.0f,
                element_idx++,
                {}
            });
        }
        else if (dev.classname == "IndicatorLight") {
            float conductance = read_param_float(dev, "conductance", 1.0f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                conductance,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "CurrentSense") {
            float conductance = read_param_float(dev, "conductance", 1000.0f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                conductance,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "RefNode") {
            float value = read_param_float(dev, "value", 0.0f);
            uint32_t node_a = resolve_port(dev, "v");
            raw_elements.push_back({
                ElectricalElementKind::FixedVoltageNode,
                node_a,
                UINT32_MAX,
                value,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "ElectricalConductance") {
            float conductance = read_param_float(dev, "conductance", 0.1f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                conductance,
                0.0f,
                element_idx++,
                {}
            });
        }
        else if (dev.classname == "ElectricalSource") {
            float voltage = read_param_float(dev, "voltage", 28.0f);
            float resistance = read_param_float(dev, "resistance", 0.01f);
            uint32_t node_pos = resolve_port(dev, "v_out");
            uint32_t node_neg = resolve_port(dev, "v_in");
            raw_elements.push_back({
                ElectricalElementKind::TheveninSource,
                node_pos,
                node_neg,
                voltage,
                resistance,
                element_idx++,
                {}
            });
        }
        else if (dev.classname == "ControlledVoltageSource") {
            float r_internal_val = read_param_float(dev, "r_internal", 0.1f);
            float initial_voltage = 0.0f;
            uint32_t node_pos = resolve_port(dev, "v_pos");
            uint32_t node_neg = resolve_port(dev, "v_neg");
            raw_elements.push_back({
                ElectricalElementKind::TheveninSource,
                node_pos,
                node_neg,
                initial_voltage,
                r_internal_val,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "VariableConductance") {
            float initial_g = 0.001f;
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                initial_g,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "AZS") {
            float g_open_val = read_param_float(dev, "g_open", 1e-6f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                g_open_val,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "HoldButton") {
            float g_open_val = read_param_float(dev, "g_open", 1e-6f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                g_open_val,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (dev.classname == "Relay") {
            float g_open_val = read_param_float(dev, "g_open", 1e-6f);
            uint32_t node_a = resolve_port(dev, "v_in");
            uint32_t node_b = resolve_port(dev, "v_out");
            raw_elements.push_back({
                ElectricalElementKind::ConductanceBranch,
                node_a,
                node_b,
                g_open_val,
                0.0f,
                element_idx++,
                dev.name
            });
        }
        else if (is_knob_switch_family(dev.classname)) {
            int positions = static_cast<int>(read_param_float(dev, "positions", 2.0f));
            positions = std::clamp(positions, 2, KnobSwitch<JitProvider>::MAX_POSITIONS);
            int initial_pos = static_cast<int>(read_param_float(dev, "initial_position", 0.0f));
            initial_pos = std::clamp(initial_pos, 0, positions - 1);
            float g_open_val = read_param_float(dev, "g_open", 1e-6f);
            float g_closed_val = read_param_float(dev, "g_closed", 1000.0f);
            uint32_t node_wiper = resolve_port(dev, "wiper");
            const char* terminal_names[] = {"throw1", "throw2", "throw3", "throw4", "throw5"};
            for (int i = 0; i < positions; ++i) {
                uint32_t node_t = resolve_port(dev, terminal_names[i]);
                float initial_g = (i == initial_pos) ? g_closed_val : g_open_val;
                raw_elements.push_back({
                    ElectricalElementKind::ConductanceBranch,
                    node_wiper,
                    node_t,
                    initial_g,
                    0.0f,
                    element_idx++,
                    dev.name
                });
            }
        }
    }

    // Build connected islands using union-find on node indices
    if (!raw_elements.empty()) {
        // Collect all unique node indices referenced by elements
        std::unordered_set<uint32_t> all_nodes;
        for (const auto& elem : raw_elements) {
            all_nodes.insert(elem.node_a);
            if (elem.node_b != UINT32_MAX) {
                all_nodes.insert(elem.node_b);
            }
        }

        // Union-find over node indices
        struct NodeUnionFind {
            std::unordered_map<uint32_t, uint32_t> parent;
            std::unordered_map<uint32_t, uint32_t> rank;

            explicit NodeUnionFind(const std::unordered_set<uint32_t>& nodes) {
                for (uint32_t n : nodes) {
                    parent[n] = n;
                    rank[n] = 0;
                }
            }

            uint32_t find(uint32_t x) {
                auto it = parent.find(x);
                if (it == parent.end() || it->second == x) {
                    return x;
                }
                it->second = find(it->second);
                return it->second;
            }

            void unite(uint32_t a, uint32_t b) {
                uint32_t ra = find(a);
                uint32_t rb = find(b);
                if (ra == rb) return;
                if (rank[ra] < rank[rb]) {
                    parent[ra] = rb;
                } else if (rank[ra] > rank[rb]) {
                    parent[rb] = ra;
                } else {
                    parent[rb] = ra;
                    rank[ra]++;
                }
            }
        };

        NodeUnionFind uf(all_nodes);

        // Unite nodes connected by each element
        for (const auto& elem : raw_elements) {
            if (elem.node_b != UINT32_MAX) {
                uf.unite(elem.node_a, elem.node_b);
            }
        }

        // Group elements by their island (root node)
        std::map<uint32_t, std::vector<size_t>> island_members;
        for (size_t i = 0; i < raw_elements.size(); ++i) {
            uint32_t root = uf.find(raw_elements[i].node_a);
            island_members[root].push_back(i);
        }

        // Create ElectricalIslandPlan for each island
        // Sort islands by smallest signal index for determinism
        std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
            island_members.begin(), island_members.end());
        std::sort(sorted_islands.begin(), sorted_islands.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [root, elem_indices] : sorted_islands) {
            (void)root;
            ElectricalIslandPlan island;

            // Collect unique signal indices in this island
            std::set<uint32_t> island_nodes;
            for (size_t idx : elem_indices) {
                island_nodes.insert(raw_elements[idx].node_a);
                if (raw_elements[idx].node_b != UINT32_MAX) {
                    island_nodes.insert(raw_elements[idx].node_b);
                }
            }
            island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

            // Build elements in original insertion order
            std::vector<size_t> sorted_indices(elem_indices.begin(), elem_indices.end());
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

            result.electrical_plan.islands.push_back(std::move(island));
        }
    }

    // == Batch 3: Assign ElectricalPrimitiveHandle to wrapper components ==
    // Build O(1) lookup: component_index -> device_name
    std::unordered_map<uint32_t, std::string> comp_idx_to_device;
    comp_idx_to_device.reserve(raw_elements.size());
    for (const auto& raw_elem : raw_elements) {
        if (!raw_elem.device_name.empty()) {
            comp_idx_to_device[static_cast<uint32_t>(raw_elem.component_index)] = raw_elem.device_name;
        }
    }

    // Assign handles from island elements
    for (size_t island_idx = 0; island_idx < result.electrical_plan.islands.size(); ++island_idx) {
        const auto& island = result.electrical_plan.islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = comp_idx_to_device.find(elem.component_index);
            if (it_name == comp_idx_to_device.end()) {
                continue;  // Element without handle (e.g. Resistor)
            }
            const std::string& device_name = it_name->second;
            auto it = result.devices.find(device_name);
            if (it == result.devices.end()) {
                throw std::runtime_error("Handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }
            ElectricalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.component_index = elem.component_index;
            // Assign handle to the appropriate component variant
            std::visit([&](auto& comp) {
                using CompType = std::decay_t<decltype(comp)>;
                if constexpr (std::is_same_v<CompType, KnobSwitch<JitProvider>> ||
                              std::is_same_v<CompType, RotarySwitch1ToN<JitProvider>> ||
                              std::is_same_v<CompType, RotarySwitchNTo1<JitProvider>>) {
                    // KnobSwitch (and aliases) has multiple handles (one per terminal branch).
                    // Assign sequentially to electrical_handles[] array.
                    if (comp.num_handles < KnobSwitch<JitProvider>::MAX_POSITIONS) {
                        comp.electrical_handles[comp.num_handles++] = handle;
                    }
                } else if constexpr (std::is_same_v<CompType, Generator<JitProvider>> ||
                              std::is_same_v<CompType, IndicatorLight<JitProvider>> ||
                              std::is_same_v<CompType, CurrentSense<JitProvider>> ||
                              std::is_same_v<CompType, ControlledVoltageSource<JitProvider>> ||
                              std::is_same_v<CompType, VariableConductance<JitProvider>> ||
                              std::is_same_v<CompType, AZS<JitProvider>> ||
                              std::is_same_v<CompType, HoldButton<JitProvider>> ||
                              std::is_same_v<CompType, Relay<JitProvider>>) {
                    comp.electrical_handle = handle;
                }
            }, it->second);
        }
    }
}

void populate_solver_owned_refs(BuildResult& result)
{
    // == E-002: Populate solver-owned typed pointer lists ==
    // Pre-build typed pointers at build time to eliminate per-frame std::visit
    for (auto& [name, variant] : result.devices) {
        (void)name;
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (std::is_same_v<T, ControlledVoltageSource<JitProvider>>) {
                result.solver_owned.controlled_voltage_sources.push_back(&comp);
            } else if constexpr (std::is_same_v<T, VariableConductance<JitProvider>>) {
                result.solver_owned.variable_conductances.push_back(&comp);
            } else if constexpr (std::is_same_v<T, AZS<JitProvider>>) {
                result.solver_owned.azs_switches.push_back(&comp);
            } else if constexpr (std::is_same_v<T, HoldButton<JitProvider>>) {
                result.solver_owned.hold_buttons.push_back(&comp);
            } else if constexpr (std::is_same_v<T, Relay<JitProvider>>) {
                result.solver_owned.relays.push_back(&comp);
            } else if constexpr (std::is_same_v<T, KnobSwitch<JitProvider>> ||
                               std::is_same_v<T, RotarySwitch1ToN<JitProvider>> ||
                               std::is_same_v<T, RotarySwitchNTo1<JitProvider>>) {
                result.solver_owned.knob_switches.push_back(&comp);
            } else if constexpr (std::is_same_v<T, Generator<JitProvider>>) {
                result.solver_owned.generators.push_back(&comp);
            } else if constexpr (std::is_same_v<T, Resistor<JitProvider>>) {
                result.solver_owned.resistors.push_back(&comp);
            } else if constexpr (std::is_same_v<T, ElectricalConductance<JitProvider>>) {
                result.solver_owned.electrical_conductances.push_back(&comp);
            } else if constexpr (std::is_same_v<T, ElectricalSource<JitProvider>>) {
                result.solver_owned.electrical_sources.push_back(&comp);
            }
        }, variant);
    }
}

}  // namespace
