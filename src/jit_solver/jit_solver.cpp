#include "jit_solver.h"
#include "scheduling.h"
#include "state.h"
#include "components/all.h"
#include "components/port_registry.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace {

// string_to_port_name is now auto-generated in port_registry.h
// If a new component's port is missing, re-run codegen.

/// Setup port indices for a component from port_to_signal mapping.
/// Ports not in the PortNames enum are silently skipped — this allows
/// user-defined blueprint ports (like alias ports) to work without codegen.
template <typename T>
void setup_component_ports(T& comp, const DeviceInstance& dev, const BuildResult& result) {
    for (const auto& [port_name, port] : dev.ports) {
        std::string port_key = dev.name + "." + port_name;
        auto it = result.port_to_signal.find(port_key);
        if (it != result.port_to_signal.end()) {
            auto port_enum = string_to_port_name(port_name);
            if (port_enum) {
                comp.provider.set(*port_enum, it->second);
            } else {
                spdlog::critical("[build] FATAL: unknown port name '{}' on device '{}'. "
                    "Re-run codegen to update port_registry.h!",
                    port_name, dev.name);
                std::abort();
            }
        }
    }
}

/// Helper to get float param with default
auto get_float = [](const DeviceInstance& dev, const std::string& key, float default_val) -> float {
    auto it = dev.params.find(key);
    if (it != dev.params.end()) {
        try {
            return std::stof(it->second);
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
};

/// Helper to get bool param with default
auto get_bool = [](const DeviceInstance& dev, const std::string& key, bool default_val) -> bool {
    auto it = dev.params.find(key);
    if (it != dev.params.end()) {
        return it->second == "true" || it->second == "1";
    }
    return default_val;
};

/// Helper to get string param with default
auto get_string = [](const DeviceInstance& dev, const std::string& key, const std::string& default_val) -> std::string {
    auto it = dev.params.find(key);
    if (it != dev.params.end()) {
        return it->second;
    }
    return default_val;
};

/// Factory function - creates a ComponentVariant from DeviceInstance
ComponentVariant create_component_variant(
    const DeviceInstance& dev,
    BuildResult& result
) {
    if (dev.classname == "Battery") {
        Battery<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.v_nominal = get_float(dev, "v_nominal", 28.0f);
        comp.internal_r = get_float(dev, "internal_r", 0.01f);
        comp.capacity = get_float(dev, "capacity", 1000.0f);
        comp.charge = get_float(dev, "charge", comp.capacity);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Switch") {
        Switch<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.closed = get_bool(dev, "initial_state", false);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "AZS") {
        AZS<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.closed = get_bool(dev, "closed", false);
        comp.i_nominal = get_float(dev, "i_nominal", 20.0f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Relay") {
        Relay<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.closed = get_bool(dev, "initial_state", false);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Resistor") {
        Resistor<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.conductance = get_float(dev, "conductance", 0.1f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Load") {
        Load<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.conductance = get_float(dev, "conductance", 0.1f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Comparator") {
        Comparator<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.Von = get_float(dev, "Von", 0.1f);
        comp.Voff = get_float(dev, "Voff", -0.1f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "HoldButton") {
        HoldButton<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.is_pressed = get_bool(dev, "initial_state", false);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Generator") {
        Generator<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.v_nominal = get_float(dev, "v_nominal", 28.5f);
        comp.internal_r = get_float(dev, "internal_r", 0.005f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "GS24") {
        GS24<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.mode = GS24Mode::STARTER;
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Transformer") {
        Transformer<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.ratio = get_float(dev, "ratio", 1.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Inverter") {
        Inverter<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.efficiency = get_float(dev, "efficiency", 0.95f);
        comp.frequency = get_float(dev, "frequency", 400.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "LerpNode") {
        LerpNode<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.factor = get_float(dev, "factor", 1.0f);
        comp.deadzone = get_float(dev, "deadzone", 0.001f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Splitter") {
        Splitter<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Merger") {
        Merger<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "IndicatorLight") {
        IndicatorLight<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.max_brightness = get_float(dev, "max_brightness", 100.0f);
        comp.conductance = get_float(dev, "conductance", 1.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Voltmeter") {
        Voltmeter<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "HighPowerLoad") {
        HighPowerLoad<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.power_draw = get_float(dev, "power_draw", 500.0f);
        comp.min_voltage_diff = get_float(dev, "min_voltage_diff", 0.01f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "ElectricPump") {
        ElectricPump<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.max_pressure = get_float(dev, "max_pressure", 1000.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "SolenoidValve") {
        SolenoidValve<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.normally_closed = get_bool(dev, "normally_closed", true);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "InertiaNode") {
        InertiaNode<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.mass = get_float(dev, "mass", 1.0f);
        comp.damping = get_float(dev, "damping", 0.5f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Spring") {
        Spring<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.k = get_float(dev, "k", 1000.0f);
        comp.c = get_float(dev, "c", 10.0f);
        comp.rest_length = get_float(dev, "rest_length", 0.1f);
        comp.compression_only = get_bool(dev, "compression_only", true);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "TempSensor") {
        TempSensor<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.sensitivity = get_float(dev, "sensitivity", 1.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "ElectricHeater") {
        ElectricHeater<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.max_power = get_float(dev, "max_power", 1000.0f);
        comp.efficiency = get_float(dev, "efficiency", 0.9f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Radiator") {
        Radiator<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.cooling_capacity = get_float(dev, "cooling_capacity", 1000.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "DMR400") {
        DMR400<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.connect_threshold = get_float(dev, "connect_threshold", 2.0f);
        comp.disconnect_threshold = get_float(dev, "disconnect_threshold", 10.0f);
        comp.min_voltage_to_close = get_float(dev, "min_voltage_to_close", 20.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "RUG82") {
        RUG82<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.v_target = get_float(dev, "v_target", 28.5f);
        comp.kp = get_float(dev, "kp", 2.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "RU19A") {
        RU19A<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.target_rpm = get_float(dev, "target_rpm", 16000.0f);
        comp.spinup_inertia = get_float(dev, "spinup_inertia", 1.0f);
        comp.spindown_inertia = get_float(dev, "spindown_inertia", 0.02f);
        comp.crank_time = get_float(dev, "crank_time", 2.0f);
        comp.ignition_time = get_float(dev, "ignition_time", 3.0f);
        comp.t4_target = get_float(dev, "t4_target", 400.0f);
        comp.t4_max = get_float(dev, "t4_max", 750.0f);
        comp.auto_start = get_bool(dev, "auto_start", true);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Gyroscope") {
        Gyroscope<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.conductance = get_float(dev, "conductance", 0.001f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "AGK47") {
        AGK47<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.conductance = get_float(dev, "conductance", 0.001f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Bus") {
        Bus<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "BlueprintInput") {
        BlueprintInput<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.exposed_type_str = get_string(dev, "exposed_type", "V");
        comp.exposed_direction_str = get_string(dev, "exposed_direction", "In");
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "BlueprintOutput") {
        BlueprintOutput<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.exposed_type_str = get_string(dev, "exposed_type", "V");
        comp.exposed_direction_str = get_string(dev, "exposed_direction", "Out");
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "RefNode") {
        RefNode<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.value = get_float(dev, "value", 0.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Subtract") {
        Subtract<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Multiply") {
        Multiply<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Divide") {
        Divide<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Add") {
        Add<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "AND") {
        AND<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "OR") {
        OR<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "XOR") {
        XOR<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "NOT") {
        NOT<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "NAND") {
        NAND<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Any_V_to_Bool") {
        Any_V_to_Bool<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Positive_V_to_Bool") {
        Positive_V_to_Bool<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "PID") {
        PID<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.Kp = get_float(dev, "Kp", 1.0f);
        comp.Ki = get_float(dev, "Ki", 0.0f);
        comp.Kd = get_float(dev, "Kd", 0.0f);
        comp.output_min = get_float(dev, "output_min", -1000.0f);
        comp.output_max = get_float(dev, "output_max", 1000.0f);
        comp.filter_alpha = get_float(dev, "filter_alpha", 0.2f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "PI") {
        PI<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.Kp = get_float(dev, "Kp", 1.0f);
        comp.Ki = get_float(dev, "Ki", 0.0f);
        comp.output_min = get_float(dev, "output_min", -1000.0f);
        comp.output_max = get_float(dev, "output_max", 1000.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "PD") {
        PD<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.Kp = get_float(dev, "Kp", 1.0f);
        comp.Kd = get_float(dev, "Kd", 0.0f);
        comp.output_min = get_float(dev, "output_min", -1000.0f);
        comp.output_max = get_float(dev, "output_max", 1000.0f);
        comp.filter_alpha = get_float(dev, "filter_alpha", 0.2f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "P") {
        P<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.Kp = get_float(dev, "Kp", 1.0f);
        comp.output_min = get_float(dev, "output_min", -1000.0f);
        comp.output_max = get_float(dev, "output_max", 1000.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "LUT") {
        LUT<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        std::string table_str = get_string(dev, "table", "");
        std::vector<float> keys, values;
        if (LUT<JitProvider>::parse_table(table_str, keys, values)) {
            comp.table_offset = static_cast<uint32_t>(result.lut_keys.size());
            comp.table_size   = static_cast<uint16_t>(keys.size());
            result.lut_keys.insert(result.lut_keys.end(), keys.begin(), keys.end());
            result.lut_values.insert(result.lut_values.end(), values.begin(), values.end());
        }
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "FastTMO") {
        FastTMO<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.tau = get_float(dev, "tau", 0.1f);
        comp.deadzone = get_float(dev, "deadzone", 0.001f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "AsymTMO") {
        AsymTMO<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.tau_up = get_float(dev, "tau_up", 0.1f);
        comp.tau_down = get_float(dev, "tau_down", 0.5f);
        comp.deadzone = get_float(dev, "deadzone", 0.001f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "SlewRate") {
        SlewRate<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.max_rate = get_float(dev, "max_rate", 1.0f);
        comp.deadzone = get_float(dev, "deadzone", 0.0001f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "AsymSlewRate") {
        AsymSlewRate<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.rate_up = get_float(dev, "rate_up", 1.0f);
        comp.rate_down = get_float(dev, "rate_down", 0.5f);
        comp.deadzone = get_float(dev, "deadzone", 0.0001f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "TimeDelay") {
        TimeDelay<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.delay_on = get_float(dev, "delay_on", 0.5f);
        comp.delay_off = get_float(dev, "delay_off", 0.1f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Monostable") {
        Monostable<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.duration = get_float(dev, "duration", 30.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "SampleHold") {
        SampleHold<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Integrator") {
        Integrator<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.gain = get_float(dev, "gain", 1.0f);
        comp.initial_val = get_float(dev, "initial_val", 0.0f);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Clamp") {
        Clamp<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        float a = get_float(dev, "min", 0.0f);
        float b = get_float(dev, "max", 1.0f);
        comp.min = std::min(a, b);
        comp.max = std::max(a, b);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Normalize") {
        Normalize<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.min = get_float(dev, "min", 0.0f);
        comp.max = get_float(dev, "max", 100.0f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Min") {
        Min<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Max") {
        Max<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Greater") {
        Greater<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "Lesser") {
        Lesser<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "GreaterEq") {
        GreaterEq<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else if (dev.classname == "LesserEq") {
        LesserEq<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
    else {
        throw std::runtime_error("Unknown component type: " + dev.classname);
    }
}

} // anonymous namespace



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
            parent_[x] = find(parent_[x]);
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
        for (const auto& [port_name, port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
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
                std::string full_port = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;

                auto it_port = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf.unite(it_port->second, it_alias->second);
                    spdlog::debug("[build] alias: {} -> {}", full_port, full_alias);
                }
            }
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

    // Mark all RefNode signals as fixed (voltage references maintain constant potential)
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            std::string v = dev.name + ".v";
            auto it = result.port_to_signal.find(v);
            if (it != result.port_to_signal.end()) {
                result.fixed_signals.push_back(it->second);
                spdlog::debug("[build] fixed signal {} for {}", it->second, dev.name);
            }
        }
    }
    std::sort(result.fixed_signals.begin(), result.fixed_signals.end());
    result.fixed_signals.erase(
        std::unique(result.fixed_signals.begin(), result.fixed_signals.end()),
        result.fixed_signals.end()
    );

    spdlog::info("[build] signal map: {} roots -> {} signals, {} fixed",
        unique_roots.size(), result.signal_count, result.fixed_signals.size());

    // Create components dynamically using factory
    for (const auto& dev : devices) {
        // Skip visual-only devices (no simulation behavior, e.g. Group)
        if (dev.visual_only) continue;

        ComponentVariant variant = create_component_variant(dev, result);
        result.devices[dev.name] = variant;

        // Add to domain-specific vectors for zero-branch iteration
        // Components can belong to multiple domains (e.g., ElectricHeater: electrical + thermal)
        ComponentVariant* ptr = &result.devices[dev.name];
        Domain domain_mask = get_component_domain_mask(variant);

        // Log domain assignment for debugging
        spdlog::debug("[build] {} -> [{}] domains", dev.name, get_domain_mask_string(domain_mask));

        // Add component to each domain it belongs to
        if (has_domain(domain_mask, Domain::Electrical)) {
            result.domain_components.electrical.push_back(ptr);
        }
        if (has_domain(domain_mask, Domain::Logical)) {
            result.domain_components.logical.push_back(ptr);
        }
        if (has_domain(domain_mask, Domain::Mechanical)) {
            result.domain_components.mechanical.push_back(ptr);
        }
        if (has_domain(domain_mask, Domain::Hydraulic)) {
            result.domain_components.hydraulic.push_back(ptr);
        }
        if (has_domain(domain_mask, Domain::Thermal)) {
            result.domain_components.thermal.push_back(ptr);
            spdlog::info("[build] {} -> THERMAL domain", dev.name);
        }
    }

    spdlog::warn("[build] created {} components (elec={}, logic={}, mech={}, hyd={}, therm={})",
        result.devices.size(),
        result.domain_components.electrical.size(),
        result.domain_components.logical.size(),
        result.domain_components.mechanical.size(),
        result.domain_components.hydraulic.size(),
        result.domain_components.thermal.size());

    return result;
}
