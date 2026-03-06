#include "jit_solver.h"
#include "component.h"
#include "systems.h"
#include "state.h"
#include "components/all.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>

namespace an24 {

// Helper: get f32 parameter with default
static float get_param_f32(const DeviceInstance& device, const std::string& key, float default_val) {
    auto it = device.params.find(key);
    if (it == device.params.end()) return default_val;
    try {
        return std::stof(it->second);
    } catch (...) {
        return default_val;
    }
}

// Helper: get signal index for port
static uint32_t get_signal_idx(
    const DeviceInstance& device,
    const std::string& port_name,
    const PortToSignal& port_to_signal,
    uint32_t signal_count
) {
    std::string full_port = device.name + "." + port_name;
    auto it = port_to_signal.find(full_port);
    if (it != port_to_signal.end()) {
        return it->second;
    }
    return signal_count - 1;  // sentinel (last allocated signal, safe to read/write)
}

std::unique_ptr<Component> create_component(
    const DeviceInstance& device,
    const PortToSignal& port_to_signal,
    uint32_t signal_count
) {
    spdlog::debug("[jit] create_component: {} {}", device.classname, device.name);

    // Helper to get port index
    auto get_port = [&](const char* port_name) {
        return get_signal_idx(device, port_name, port_to_signal, signal_count);
    };

    // Helper to get float param
    auto get_f = [&](const char* key, float def) {
        return get_param_f32(device, key, def);
    };

    if (device.classname == "Battery") {
        auto bat = std::make_unique<Battery>(
            get_port("v_in"), get_port("v_out"),
            get_f("v_nominal", 28.0f), get_f("internal_r", 0.01f));
        bat->name = device.name;
        return bat;
    }
    else if (device.classname == "Switch") {
        bool is_closed = device.params.count("closed") ?
            (device.params.at("closed") == "true") : false;  // Default: open
        return std::make_unique<Switch>(
            get_port("v_in"), get_port("v_out"), get_port("control"), get_port("state"), is_closed);
    }
    else if (device.classname == "HoldButton") {
        return std::make_unique<HoldButton>(
            get_port("v_in"), get_port("v_out"), get_port("control"), get_port("state"));
    }
    else if (device.classname == "Relay") {
        bool is_closed = device.params.count("closed") ?
            (device.params.at("closed") == "true") : false;  // Default: open
        return std::make_unique<Relay>(
            get_port("v_in"), get_port("v_out"), get_port("control"), is_closed);
    }
    else if (device.classname == "Resistor") {
        return std::make_unique<Resistor>(
            get_port("v_in"), get_port("v_out"),
            get_f("conductance", 0.1f));
    }
    else if (device.classname == "RefNode") {
        return std::make_unique<RefNode>(
            get_port("v"), get_f("value", 0.0f));
    }
    else if (device.classname == "Bus") {
        // Bus connects all ports to the same signal
        // Just use first port's signal index
        return std::make_unique<Bus>(get_port("v"));
    }
    else if (device.classname == "Splitter") {
        // Splitter connects all ports to the same signal (like Bus)
        // Just use real port's signal index (virtual aliases merged via union-find)
        return std::make_unique<Splitter>(get_port("i"));
    }
    else if (device.classname == "Generator") {
        return std::make_unique<Generator>(
            get_port("v_in"), get_port("v_out"),
            get_f("v_nominal", 28.5f), get_f("internal_r", 0.005f));
    }
    else if (device.classname == "GS24") {
        auto gs = std::make_unique<GS24>(
            get_port("v_in"), get_port("v_out"),
            get_f("v_nominal", 28.5f), get_f("r_internal", 0.025f));
        gs->target_rpm = get_f("target_rpm", 16000.0f);
        gs->r_norton = get_f("r_norton", 0.08f);
        gs->k_mod_idx = get_port("k_mod");  // connect to RUG82
        return gs;
    }
    else if (device.classname == "RUG82") {
        auto rug = std::make_unique<RUG82>(
            get_port("v_gen"), get_port("k_mod"));
        return rug;
    }
    else if (device.classname == "RU19A") {
        auto apu = std::make_unique<RU19A>(
            get_port("v_start"), get_port("v_bus"), get_port("k_mod"),
            get_port("rpm_out"), get_port("t4_out"));
        return apu;
    }
    else if (device.classname == "DMR400") {
        auto dmr = std::make_unique<DMR400>(
            get_port("v_gen_ref"), get_port("v_in"),
            get_port("v_out"), get_port("lamp"));
        // Will be linked to RU19A after all components created
        return dmr;
    }
    else if (device.classname == "Gyroscope") {
        auto gyro = std::make_unique<Gyroscope>(get_port("input"));
        gyro->conductance = get_f("conductance", 0.001f);
        return gyro;
    }
    else if (device.classname == "AGK47") {
        auto agk = std::make_unique<AGK47>(get_port("input"));
        agk->conductance = get_f("conductance", 0.001f);
        return agk;
    }
    else if (device.classname == "Load") {
        return std::make_unique<Load>(
            get_port("input"),
            get_f("conductance", 0.1f));
    }
    else if (device.classname == "Transformer") {
        return std::make_unique<Transformer>(
            get_port("primary"), get_port("secondary"), get_f("ratio", 1.0f));
    }
    else if (device.classname == "Inverter") {
        return std::make_unique<Inverter>(
            get_port("dc_in"), get_port("ac_out"),
            get_f("efficiency", 0.95f), get_f("frequency", 400.0f));
    }
    else if (device.classname == "LerpNode") {
        return std::make_unique<LerpNode>(
            get_port("input"), get_port("output"), get_f("factor", 1.0f));
    }
    else if (device.classname == "IndicatorLight") {
        std::string color = device.params.count("color") ? device.params.at("color") : "white";
        return std::make_unique<IndicatorLight>(
            get_port("v_in"), get_port("v_out"), get_port("brightness"),
            get_f("max_brightness", 100.0f), color);
    }
    else if (device.classname == "HighPowerLoad") {
        return std::make_unique<HighPowerLoad>(
            get_port("v_in"), get_port("v_out"), get_f("power_draw", 500.0f));
    }
    else if (device.classname == "Gyroscope") {
        return std::make_unique<Gyroscope>(get_port("input"));
    }
    else if (device.classname == "Voltmeter") {
        return std::make_unique<Voltmeter>(get_port("v_in"));
    }
    else if (device.classname == "AGK47") {
        return std::make_unique<AGK47>(get_port("input"));
    }
    else if (device.classname == "ElectricPump") {
        return std::make_unique<ElectricPump>(
            get_port("v_in"), get_port("p_out"), get_f("max_pressure", 1000.0f));
    }
    else if (device.classname == "SolenoidValve") {
        bool nc = device.params.count("normally_closed") ?
            (device.params.at("normally_closed") == "true") : true;
        return std::make_unique<SolenoidValve>(
            get_port("ctrl"), get_port("flow_in"), get_port("flow_out"), nc);
    }
    else if (device.classname == "InertiaNode") {
        return std::make_unique<InertiaNode>(
            get_port("input"), get_port("output"),
            get_f("mass", 1.0f), get_f("damping", 0.5f));
    }
    else if (device.classname == "TempSensor") {
        return std::make_unique<TempSensor>(
            get_port("temp_in"), get_port("temp_out"), get_f("sensitivity", 1.0f));
    }
    else if (device.classname == "ElectricHeater") {
        return std::make_unique<ElectricHeater>(
            get_port("power"), get_port("heat_out"),
            get_f("max_power", 1000.0f), get_f("efficiency", 0.9f));
    }
    else if (device.classname == "Radiator") {
        return std::make_unique<Radiator>(
            get_port("heat_in"), get_port("heat_out"), get_f("cooling_capacity", 1000.0f));
    }
    else if (device.classname == "Comparator") {
        auto comp = std::make_unique<Comparator>(
            get_port("Va"), get_port("Vb"), get_port("o"));
        comp->Von = get_f("Von", 5.0f);    // Get Von parameter from JSON
        comp->Voff = get_f("Voff", 2.0f);  // Get Voff parameter from JSON
        return comp;
    }

    spdlog::warn("Unknown device type: {}", device.classname);
    return nullptr;
}

// Union-Find for port connections
class UnionFind {
    std::vector<uint32_t> parent_;
    std::vector<uint32_t> rank_;

public:
    explicit UnionFind(size_t n) : parent_(n), rank_(n, 0) {
        for (uint32_t i = 0; i < n; ++i) parent_[i] = i;
    }

    uint32_t find(uint32_t x) {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);  // path compression
        }
        return parent_[x];
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);
        if (ra == rb) return;
        if (rank_[ra] < rank_[rb]) {
            parent_[ra] = rb;
        } else if (rank_[ra] > rank_[rb]) {
            parent_[rb] = ra;
        } else {
            parent_[rb] = ra;
            rank_[ra]++;
        }
    }
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    BuildResult result;

    // Build port list
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;

    for (const auto& dev : devices) {
        for (const auto& port : dev.ports) {
            std::string full_port = dev.name + "." + port.first;
            uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(full_port);
            port_to_idx[full_port] = idx;
        }
    }

    // Union-Find for connected ports
    UnionFind uf(all_ports.size());

    // Union connected ports
    for (const auto& [from, to] : connections) {
        auto it_from = port_to_idx.find(from);
        auto it_to = port_to_idx.find(to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
        }
    }

    // Union alias ports within same device (e.g., Splitter o1/o2 -> i)
    for (const auto& dev : devices) {
        for (const auto& [port_name, port] : dev.ports) {
            if (port.alias.has_value() && !port.alias->empty()) {
                // This port is an alias - union it with its target
                std::string full_port = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;

                auto it_port = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf.unite(it_port->second, it_alias->second);
                    spdlog::debug("[jit] alias: {} -> {}", full_port, full_alias);
                }
            }
        }
    }

    // Add internal RefNode connections (v ↔ v_out - same node)
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            std::string v = dev.name + ".v";
            // RefNode has single port - no internal connection needed
        }
    }

    // Map each port to its root signal
    std::map<uint32_t, uint32_t> root_to_signal;
    for (const auto& port : all_ports) {
        uint32_t idx = port_to_idx[port];
        uint32_t root = uf.find(idx);
        result.port_to_signal[port] = root;
    }

    // Get unique roots and remap to 0-based indices
    std::vector<uint32_t> unique_roots;
    for (const auto& [port, root] : result.port_to_signal) {
        unique_roots.push_back(root);
    }
    std::sort(unique_roots.begin(), unique_roots.end());
    unique_roots.erase(std::unique(unique_roots.begin(), unique_roots.end()), unique_roots.end());

    // Create remap: old root -> new sequential index
    uint32_t next_signal = 0;
    for (uint32_t root : unique_roots) {
        root_to_signal[root] = next_signal++;
    }

    // Apply remap to port_to_signal
    for (auto& [port, sig] : result.port_to_signal) {
        sig = root_to_signal[sig];
    }

    // Count unique signals after remap
    result.signal_count = next_signal;

    // Sentinel signal for unconnected ports
    result.signal_count++;

    // Mark ground (gnd.RefNode with value=0) as fixed to provide voltage reference
    // Other RefNodes (dc_bus) are NOT marked fixed - they're just connection points
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end() && it_val->second == "0.0") {
                std::string v = dev.name + ".v";
                auto it = result.port_to_signal.find(v);
                if (it != result.port_to_signal.end()) {
                    result.fixed_signals.push_back(it->second);
                    spdlog::debug("[jit] fixed signal {} for ground reference {}", it->second, dev.name);
                }
            }
        }
    }
    std::sort(result.fixed_signals.begin(), result.fixed_signals.end());
    result.fixed_signals.erase(
        std::unique(result.fixed_signals.begin(), result.fixed_signals.end()),
        result.fixed_signals.end()
    );

    spdlog::debug("[jit] signal remap: {} unique roots -> {} signals",
        unique_roots.size(), result.signal_count);

    // Create components
    std::vector<std::string> comp_names;
    for (const auto& dev : devices) {
        auto comp = create_component(dev, result.port_to_signal, result.signal_count);
        if (!comp) continue;
        comp_names.push_back(dev.name);

        // Log domains for RU19A
        if (dev.classname == "RU19A") {
            spdlog::info("[jit] RU19A domains: {} domains", dev.domains.size());
        }

        // Add to all domains (from component definition)
        result.systems.add_component(std::move(comp), dev.domains);
    }

    result.systems.pre_load();

    spdlog::info("Built JIT systems: {} signals, {} fixed",
        result.signal_count, result.fixed_signals.size());

    return result;
}

} // namespace an24
