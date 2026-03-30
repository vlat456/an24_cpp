#include "jit_solver.h"

#include "components/battery.h"
#include "components/switch.h"
#include "components/relay.h"
#include "components/hold_button.h"
#include "components/load.h"
#include "components/ref_node.h"
#include "components/generator.h"
#include "components/bus.h"
#include "components/blueprint_input.h"
#include "components/blueprint_output.h"
#include "components/comparator.h"
#include "components/current_sense.h"
#include "components/azs.h"
#include "components/resistor.h"
#include "components/voltmeter.h"
#include "components/indicator_light.h"
#include "components/add.h"
#include "components/subtract.h"
#include "components/multiply.h"
#include "components/divide.h"
#include "components/and_gate.h"
#include "components/or_gate.h"
#include "components/xor_gate.h"
#include "components/not_gate.h"
#include "components/nand_gate.h"
#include "components/min.h"
#include "components/max.h"
#include "components/max_selector.h"
#include "components/clamp.h"
#include "components/pid.h"
#include "components/pi.h"
#include "components/pd.h"
#include "components/p.h"
#include "components/integrator.h"
#include "components/sample_hold.h"
#include "components/time_delay.h"
#include "components/monostable.h"
#include "components/slew_rate.h"
#include "components/asym_slew_rate.h"
#include "components/fast_tmo.h"
#include "components/asym_tmo.h"
#include "components/normalize.h"
#include "components/lut.h"
#include "components/greater.h"
#include "components/lesser.h"
#include "components/greater_eq.h"
#include "components/lesser_eq.h"
#include "components/any_v_to_bool.h"
#include "components/positive_v_to_bool.h"
#include "components/lerp_node.h"
#include "components/slider.h"
#include "components/splitter.h"
#include "components/merger.h"
#include "components/agk47.h"
#include "components/dmr400.h"
#include "components/electric_heater.h"
#include "components/electric_pump.h"
#include "components/fuel_tank.h"
#include "components/gidro_accumulator.h"
#include "components/gs24.h"
#include "components/gyroscope.h"
#include "components/high_power_load.h"
#include "components/inertia_node.h"
#include "components/inverter.h"
#include "components/radiator.h"
#include "components/ru19a.h"
#include "components/rug82.h"
#include "components/solenoid_valve.h"
#include "components/spring.h"
#include "components/temp_sensor.h"
#include "components/transformer.h"
#include "components/voltage_sense.h"
#include "components/controlled_voltage_source.h"
#include "components/controlled_current_source.h"
#include "components/variable_conductance.h"

#include <algorithm>
#include <map>
#include <queue>
#include <string_view>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include "../parse_number.h"

namespace {
bool is_source_component_class(std::string_view classname) {
    return classname == "Battery" || classname == "Generator" || classname == "RefNode";
}

bool is_migrated_component_class(std::string_view classname) {
    return is_source_component_class(classname) ||
           classname == "Switch" || classname == "Relay" ||
           classname == "HoldButton" || classname == "Load" ||
           classname == "Bus" || classname == "BlueprintInput" ||
           classname == "BlueprintOutput" || classname == "Comparator" ||
           classname == "CurrentSense" || classname == "AZS" ||
           classname == "Resistor" || classname == "Voltmeter" ||
           classname == "IndicatorLight" || classname == "Add" ||
           classname == "Subtract" || classname == "Multiply" ||
           classname == "Divide" || classname == "AND" || classname == "OR" ||
           classname == "XOR" || classname == "NOT" || classname == "NAND" ||
           classname == "Min" || classname == "Max" || classname == "MaxSelector" ||
           classname == "Clamp" || classname == "PID" || classname == "PI" ||
           classname == "PD" || classname == "P" || classname == "Integrator" ||
           classname == "SampleHold" || classname == "TimeDelay" ||
           classname == "Monostable" || classname == "SlewRate" ||
           classname == "AsymSlewRate" || classname == "FastTMO" ||
           classname == "AsymTMO" || classname == "Normalize" ||
           classname == "LUT" || classname == "Greater" || classname == "Lesser" ||
           classname == "GreaterEq" || classname == "LesserEq" ||
           classname == "Any_V_to_Bool" || classname == "Positive_V_to_Bool" ||
           classname == "LerpNode" || classname == "Slider" ||
           classname == "Splitter" || classname == "Merger" ||
           classname == "AGK47" || classname == "DMR400" ||
           classname == "ElectricHeater" || classname == "ElectricPump" ||
           classname == "FuelTank" || classname == "GidroAccumulator" ||
           classname == "GS24" || classname == "Gyroscope" ||
           classname == "HighPowerLoad" || classname == "InertiaNode" ||
           classname == "Inverter" || classname == "Radiator" ||
           classname == "RU19A" || classname == "RUG82" ||
           classname == "SolenoidValve" || classname == "Spring" ||
           classname == "TempSensor" || classname == "Transformer" ||
           classname == "VoltageSense" ||
           classname == "ControlledVoltageSource" ||
           classname == "ControlledCurrentSource" ||
           classname == "VariableConductance";
}

const std::unordered_set<std::string>& known_library_unused_params() {
    static const std::unordered_set<std::string> params = {
        "inv_internal_r",  // Computed in Battery/Generator pre_load() as 1.0f / internal_r
        "inv_capacity",    // Computed in Battery pre_load() as 1.0f / capacity
        "port_edge",       // Used in Bus blueprint but not consumed by Bus component
        "exposed_direction", // Used in BlueprintInput blueprint but not consumed
        "exposed_type",     // Used in BlueprintInput blueprint but not consumed
        "resistance"        // Used in Load blueprint but Load uses conductance, not resistance
    };
    return params;
}
} // namespace

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    BuildResult result{};

    // == Strict parameter helpers - fail fast on missing required params ==
    auto get_param_required = [&](const std::unordered_map<std::string, std::string>& params,
                                  const std::string& key,
                                  const DeviceInstance& dev) -> std::string {
        auto it = params.find(key);
        if (it == params.end()) {
            // Build list of available keys
            std::string available;
            for (const auto& [k, v] : params) {
                if (!available.empty()) available += ", ";
                available += k;
            }
            throw std::runtime_error("Missing required parameter '" + key +
                "' for component '" + dev.name + "' (classname: " + dev.classname +
                "). Available keys: " + available);
        }
        return it->second;
    };

    auto parse_param_float_required = [&](const std::unordered_map<std::string, std::string>& params,
                                         const std::string& key,
                                         const DeviceInstance& dev) -> float {
        std::string val = get_param_required(params, key, dev);
        return locale_safe::parse_float_or(val, 0.0f);
    };

    auto parse_param_bool_required = [&](const std::unordered_map<std::string, std::string>& params,
                                         const std::string& key,
                                         const DeviceInstance& dev) -> bool {
        std::string val = get_param_required(params, key, dev);
        return val == "true" || val == "1";
    };

    std::vector<std::string> all_ports;

    std::unordered_map<std::string, uint32_t> port_to_idx;

    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            const std::string full_port = dev.name + "." + port_name;
            const uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(full_port);
            port_to_idx[full_port] = idx;
        }
    }

    if (all_ports.empty()) {
        result.signal_count = 1; // sentinel
        return result;
    }

    struct UnionFind {
        std::vector<uint32_t> parent;
        std::vector<uint32_t> rank;

        explicit UnionFind(size_t n) : parent(n), rank(n, 0) {
            for (uint32_t i = 0; i < n; ++i) {
                parent[i] = i;
            }
        }

        uint32_t find(uint32_t x) {
            if (parent[x] != x) {
                parent[x] = find(parent[x]);
            }
            return parent[x];
        }

        void unite(uint32_t a, uint32_t b) {
            const uint32_t ra = find(a);
            const uint32_t rb = find(b);
            if (ra == rb) {
                return;
            }

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

    UnionFind uf(all_ports.size());

    for (const auto& [from, to] : connections) {
        auto it_from = port_to_idx.find(from);
        auto it_to = port_to_idx.find(to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
        }
    }

    std::map<uint32_t, uint32_t> root_to_signal;
    uint32_t next_signal = 0;
    for (const auto& [port, idx] : port_to_idx) {
        const uint32_t root = uf.find(idx);
        auto [it, inserted] = root_to_signal.emplace(root, next_signal);
        if (inserted) {
            next_signal++;
        }
        result.port_to_signal[port] = it->second;
    }

    result.signal_count = next_signal + 1; // sentinel at end

    std::vector<std::string> consumer_device_names;

    // Phase 2 Slice 1: Create and register migrated components
    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        bool is_source = is_source_component_class(dev.classname);
        bool is_migrated = is_migrated_component_class(dev.classname);

        if (!is_migrated) {
            continue;
        }

        if (!is_source) {
            consumer_device_names.push_back(dev.name);
        }

        // == Per-device consumed-key tracking for strict validation ==
        std::unordered_set<std::string> consumed_params;

        // Whitelist of known library-specified parameters that the component doesn't actually consume.
        // These appear in library JSON but are not used by the component - they're silently consumed
        // to maintain backward compatibility with existing blueprints.
        const auto& known_unused_params = known_library_unused_params();

        // == Consume helper lambdas - mark keys as used ==
        auto consume_float_optional = [&](const std::string& key, float default_val) -> float {
            consumed_params.insert(key);
            auto it = dev.params.find(key);
            if (it != dev.params.end()) {
                return locale_safe::parse_float_or(it->second, default_val);
            }
            return default_val;
        };

        auto consume_bool_optional = [&](const std::string& key, bool default_val) -> bool {
            consumed_params.insert(key);
            auto it = dev.params.find(key);
            if (it != dev.params.end()) {
                return it->second == "true" || it->second == "1";
            }
            return default_val;
        };

        auto consume_string_optional = [&](const std::string& key, const std::string& default_val) -> std::string {
            consumed_params.insert(key);
            auto it = dev.params.find(key);
            if (it != dev.params.end()) {
                return it->second;
            }
            return default_val;
        };

        // Required helpers also insert into consumed_params for strict validation
        auto consume_float_required = [&](const std::string& key) -> float {
            consumed_params.insert(key);
            return parse_param_float_required(dev.params, key, dev);
        };

        auto consume_bool_required = [&](const std::string& key) -> bool {
            consumed_params.insert(key);
            return parse_param_bool_required(dev.params, key, dev);
        };

        // Strict validation helper: throws on any unconsumed key
        // Internal computed params (inv_internal_r, inv_capacity) are silently consumed
        auto validate_all_params_consumed = [&]() {
            for (const auto& [key, val] : dev.params) {
                (void)val;
                if (consumed_params.find(key) == consumed_params.end()) {
                    // Check if it's a known library-unused param
                    if (known_unused_params.find(key) != known_unused_params.end()) {
                        consumed_params.insert(key);  // Mark as consumed silently
                        continue;
                    }
                    throw std::runtime_error("Unknown/unconsumed parameter '" + key +
                        "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
                }
            }
        };

        // Set up port indices for the provider
        auto setup_ports = [&](auto& comp) {
            for (const auto& [port_name, port] : dev.ports) {
                const std::string full_port = dev.name + "." + port_name;
                auto it = result.port_to_signal.find(full_port);
                if (it != result.port_to_signal.end()) {
                    auto port_enum = string_to_port_name(port_name);
                    if (port_enum.has_value()) {
                        comp.provider.set(port_enum.value(), it->second);
                    }
                }
            }
        };

        // Handle each component type
        if (dev.classname == "Battery") {
            Battery<JitProvider> comp;
            
            // Load parameters
            comp.v_nominal = consume_float_optional("v_nominal", 28.0f);
            comp.internal_r = consume_float_optional("internal_r", 0.01f);
            // capacity and charge are stored but not used in current battery model
            comp.capacity = consume_float_optional("capacity", 1000.0f);
            comp.charge = consume_float_optional("charge", 1000.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<Battery<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Generator") {
            Generator<JitProvider> comp;
            
            comp.v_nominal = consume_float_optional("v_nominal", 28.5f);
            comp.internal_r = consume_float_optional("internal_r", 0.005f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<Generator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RefNode") {
            RefNode<JitProvider> comp;
            
            comp.value = consume_float_optional("value", 0.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<RefNode<JitProvider>>(result.devices[dev.name]));
            
            // Also track as fixed signal
            const std::string key = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(key);
            if (it_sig != result.port_to_signal.end()) {
                result.fixed_signals.push_back(it_sig->second);
            }
        }
        else if (dev.classname == "Switch") {
            Switch<JitProvider> comp;
            
            comp.closed = consume_bool_optional("closed", false);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Switch<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Relay") {
            Relay<JitProvider> comp;
            
            comp.closed = consume_bool_optional("closed", false);
            comp.hold_threshold = consume_float_optional("hold_threshold", 0.5f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Relay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HoldButton") {
            HoldButton<JitProvider> comp;
            
            comp.idle = consume_float_optional("idle", 0.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HoldButton<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Load") {
            Load<JitProvider> comp;
            
            comp.conductance = consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Load<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Bus") {
            Bus<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Bus<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintInput") {
            BlueprintInput<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintInput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintOutput") {
            BlueprintOutput<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintOutput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Comparator") {
            Comparator<JitProvider> comp;
            
            comp.Von = consume_float_optional("Von", 5.0f);
            comp.Voff = consume_float_optional("Voff", 2.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Comparator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "CurrentSense") {
            CurrentSense<JitProvider> comp;
            
            comp.conductance = consume_float_optional("conductance", 1000.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<CurrentSense<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AZS") {
            AZS<JitProvider> comp;
            
            comp.closed = consume_bool_optional("closed", false);
            comp.i_nominal = consume_float_optional("i_nominal", 20.0f);
            comp.k_cool = consume_float_optional("k_cool", 1.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AZS<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Resistor") {
            Resistor<JitProvider> comp;
            
            comp.conductance = consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Resistor<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Voltmeter") {
            Voltmeter<JitProvider> comp;
            
            comp.min = consume_float_optional("min", 0.0f);
            comp.max = consume_float_optional("max", 28.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Voltmeter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "IndicatorLight") {
            IndicatorLight<JitProvider> comp;
            
            comp.max_brightness = consume_float_optional("max_brightness", 100.0f);
            comp.conductance = consume_float_optional("conductance", 1.0f);
            comp.rated_voltage = consume_float_optional("rated_voltage", 28.0f);
            comp.color = consume_string_optional("color", "white");
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<IndicatorLight<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 3: Logical/math components
        else if (dev.classname == "Add") {
            Add<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Add<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Subtract") {
            Subtract<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Subtract<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Multiply") {
            Multiply<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Multiply<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Divide") {
            Divide<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Divide<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AND") {
            AND<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "OR") {
            OR<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<OR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "XOR") {
            XOR<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<XOR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NOT") {
            NOT<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NOT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NAND") {
            NAND<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NAND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Min") {
            Min<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Min<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Max" || dev.classname == "MaxSelector") {
            Max<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Max<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Clamp") {
            Clamp<JitProvider> comp;
            
            comp.min = consume_float_optional("min", 0.0f);
            comp.max = consume_float_optional("max", 1.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Clamp<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 4: Control/filter components
        else if (dev.classname == "PID") {
            PID<JitProvider> comp;
            
            comp.Kp = consume_float_required("Kp");
            comp.Ki = consume_float_required("Ki");
            comp.Kd = consume_float_required("Kd");
            comp.output_min = consume_float_required("output_min");
            comp.output_max = consume_float_required("output_max");
            comp.filter_alpha = consume_float_required("filter_alpha");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PID<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PI") {
            PI<JitProvider> comp;
            
            comp.Kp = consume_float_required("Kp");
            comp.Ki = consume_float_required("Ki");
            comp.output_min = consume_float_required("output_min");
            comp.output_max = consume_float_required("output_max");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PI<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PD") {
            PD<JitProvider> comp;
            
            comp.Kp = consume_float_required("Kp");
            comp.Kd = consume_float_required("Kd");
            comp.filter_alpha = consume_float_required("filter_alpha");
            comp.output_min = consume_float_required("output_min");
            comp.output_max = consume_float_required("output_max");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PD<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "P") {
            P<JitProvider> comp;
            
            comp.Kp = consume_float_required("Kp");
            comp.output_min = consume_float_required("output_min");
            comp.output_max = consume_float_required("output_max");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<P<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Integrator") {
            Integrator<JitProvider> comp;
            
            comp.gain = consume_float_required("gain");
            comp.initial_val = consume_float_required("initial_val");
            comp.accumulator = comp.initial_val;
            comp.next_accumulator = comp.initial_val;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Integrator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SampleHold") {
            SampleHold<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SampleHold<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "TimeDelay") {
            TimeDelay<JitProvider> comp;
            
            // Check if "delay" fallback is used (sets both if present)
            bool has_delay = dev.params.find("delay") != dev.params.end();
            bool has_delay_on = dev.params.find("delay_on") != dev.params.end();
            bool has_delay_off = dev.params.find("delay_off") != dev.params.end();
            
            if (has_delay) {
                float d = consume_float_required("delay");
                comp.delay_on = d;
                comp.delay_off = d;
            }
            if (has_delay_on) {
                comp.delay_on = consume_float_required("delay_on");
            }
            if (has_delay_off) {
                comp.delay_off = consume_float_required("delay_off");
            }
            // Require at least one of delay/delay_on/delay_off
            if (!has_delay && !has_delay_on && !has_delay_off) {
                throw std::runtime_error("Missing required parameter for TimeDelay '" + dev.name +
                    "': must have 'delay', or 'delay_on'/'delay_off'. Available keys: ");
            }
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TimeDelay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Monostable") {
            Monostable<JitProvider> comp;
            
            comp.duration = consume_float_required("duration");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Monostable<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SlewRate") {
            SlewRate<JitProvider> comp;
            
            comp.max_rate = consume_float_required("max_rate");
            comp.deadzone = consume_float_required("deadzone");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymSlewRate") {
            AsymSlewRate<JitProvider> comp;
            
            comp.rate_up = consume_float_required("rate_up");
            comp.rate_down = consume_float_required("rate_down");
            comp.deadzone = consume_float_required("deadzone");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymSlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FastTMO") {
            FastTMO<JitProvider> comp;
            
            comp.tau = consume_float_required("tau");
            comp.deadzone = consume_float_optional("deadzone", 0.001f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FastTMO<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymTMO") {
            AsymTMO<JitProvider> comp;
            
            comp.tau_up = consume_float_required("tau_up");
            comp.tau_down = consume_float_required("tau_down");
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymTMO<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 5: Signal-shaping / utility logical components
        else if (dev.classname == "Normalize") {
            Normalize<JitProvider> comp;
            
            comp.min = consume_float_optional("min", 0.0f);
            comp.max = consume_float_optional("max", 100.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Normalize<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LUT") {
            LUT<JitProvider> comp;
            
            // Parse table data from "table" param into arena
            if (auto it = dev.params.find("table"); it != dev.params.end()) {
                consumed_params.insert("table");
                std::vector<float> keys, vals;
                if (LUT<JitProvider>::parse_table(it->second, keys, vals)) {
                    comp.table_offset = static_cast<uint32_t>(result.lut_keys.size());
                    comp.table_size = static_cast<uint16_t>(keys.size());
                    result.lut_keys.insert(result.lut_keys.end(), keys.begin(), keys.end());
                    result.lut_values.insert(result.lut_values.end(), vals.begin(), vals.end());
                }
            }
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LUT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Greater") {
            Greater<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Greater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Lesser") {
            Lesser<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Lesser<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GreaterEq") {
            GreaterEq<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GreaterEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LesserEq") {
            LesserEq<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LesserEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Any_V_to_Bool") {
            Any_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Any_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Positive_V_to_Bool") {
            Positive_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Positive_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LerpNode") {
            LerpNode<JitProvider> comp;
            
            comp.factor = consume_float_required("factor");
            comp.deadzone = consume_float_required("deadzone");
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LerpNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Slider") {
            Slider<JitProvider> comp;
            
            comp.min = consume_float_optional("min", 0.0f);
            comp.max = consume_float_optional("max", 1.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Slider<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Splitter") {
            Splitter<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Splitter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Merger") {
            Merger<JitProvider> comp;
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Merger<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 6: Additional non-controlled components
        else if (dev.classname == "AGK47") {
            AGK47<JitProvider> comp;
            
            comp.conductance = consume_float_optional("conductance", 0.001f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AGK47<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "DMR400") {
            DMR400<JitProvider> comp;
            
            comp.connect_threshold = consume_float_optional("connect_threshold", 2.0f);
            comp.disconnect_threshold = consume_float_optional("disconnect_threshold", 10.0f);
            comp.min_voltage_to_close = consume_float_optional("min_voltage_to_close", 20.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<DMR400<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricHeater") {
            ElectricHeater<JitProvider> comp;
            
            comp.max_power = consume_float_optional("max_power", 1000.0f);
            comp.efficiency = consume_float_optional("efficiency", 0.9f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricHeater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricPump") {
            ElectricPump<JitProvider> comp;
            
            comp.max_pressure = consume_float_optional("max_pressure", 1000.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricPump<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FuelTank") {
            FuelTank<JitProvider> comp;
            
            comp.capacity = consume_float_optional("capacity", 1000.0f);
            comp.level = consume_float_optional("level", 1000.0f);
            comp.density = consume_float_optional("density", 0.78f);
            comp.consumption_rate = consume_float_optional("consumption_rate", 0.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FuelTank<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GidroAccumulator") {
            GidroAccumulator<JitProvider> comp;
            
            comp.precharge_pressure = consume_float_optional("precharge_pressure", 50.0f);
            comp.volume = consume_float_optional("volume", 10.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GidroAccumulator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GS24") {
            GS24<JitProvider> comp;
            
            comp.v_nominal = consume_float_optional("v_nominal", 28.5f);
            comp.target_rpm = consume_float_optional("target_rpm", 16000.0f);
            comp.r_internal = consume_float_optional("r_internal", 0.025f);
            comp.r_norton = consume_float_optional("r_norton", 0.08f);
            comp.rpm_cutoff = consume_float_optional("rpm_cutoff", 0.45f);
            comp.rpm_threshold = consume_float_optional("rpm_threshold", 0.4f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GS24<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Gyroscope") {
            Gyroscope<JitProvider> comp;
            
            comp.conductance = consume_float_optional("conductance", 0.001f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Gyroscope<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HighPowerLoad") {
            HighPowerLoad<JitProvider> comp;
            
            comp.power_draw = consume_float_optional("power_draw", 500.0f);
            comp.min_voltage_diff = consume_float_optional("min_voltage_diff", 0.01f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HighPowerLoad<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "InertiaNode") {
            InertiaNode<JitProvider> comp;
            
            comp.mass = consume_float_optional("mass", 1.0f);
            comp.damping = consume_float_optional("damping", 0.5f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<InertiaNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Inverter") {
            Inverter<JitProvider> comp;
            
            comp.efficiency = consume_float_optional("efficiency", 0.95f);
            comp.frequency = consume_float_optional("frequency", 400.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Inverter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Radiator") {
            Radiator<JitProvider> comp;
            
            comp.cooling_capacity = consume_float_optional("cooling_capacity", 1000.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Radiator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RU19A") {
            RU19A<JitProvider> comp;
            
            comp.target_rpm = consume_float_optional("target_rpm", 16000.0f);
            comp.auto_start = consume_bool_optional("auto_start", true);
            comp.spinup_inertia = consume_float_optional("spinup_inertia", 1.0f);
            comp.spindown_inertia = consume_float_optional("spindown_inertia", 0.02f);
            comp.crank_time = consume_float_optional("crank_time", 2.0f);
            comp.ignition_time = consume_float_optional("ignition_time", 3.0f);
            comp.start_timeout = consume_float_optional("start_timeout", 30.0f);
            comp.t4_target = consume_float_optional("t4_target", 400.0f);
            comp.t4_max = consume_float_optional("t4_max", 750.0f);
            comp.ambient_temp = consume_float_optional("ambient_temp", 20.0f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RU19A<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RUG82") {
            RUG82<JitProvider> comp;
            
            comp.v_target = consume_float_optional("v_target", 28.5f);
            comp.kp = consume_float_optional("kp", 2.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RUG82<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SolenoidValve") {
            SolenoidValve<JitProvider> comp;
            
            comp.normally_closed = consume_bool_optional("normally_closed", false);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SolenoidValve<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Spring") {
            Spring<JitProvider> comp;
            
            comp.k = consume_float_optional("k", 1000.0f);
            comp.c = consume_float_optional("c", 10.0f);
            comp.rest_length = consume_float_optional("rest_length", 0.1f);
            comp.compression_only = consume_bool_optional("compression_only", false);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Spring<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "TempSensor") {
            TempSensor<JitProvider> comp;
            
            comp.sensitivity = consume_float_optional("sensitivity", 1.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TempSensor<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Transformer") {
            Transformer<JitProvider> comp;
            
            comp.ratio = consume_float_optional("ratio", 1.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Transformer<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VoltageSense") {
            VoltageSense<JitProvider> comp;
            
            comp.gain = consume_float_optional("gain", 1.0f);
            comp.offset = consume_float_optional("offset", 0.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VoltageSense<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 7: Controlled source / conductance components
        else if (dev.classname == "ControlledVoltageSource") {
            ControlledVoltageSource<JitProvider> comp;
            
            comp.gain = consume_float_optional("gain", 1.0f);
            comp.offset = consume_float_optional("offset", 0.0f);
            comp.min_v = consume_float_optional("min_v", 0.0f);
            comp.max_v = consume_float_optional("max_v", 30.0f);
            comp.r_internal = consume_float_optional("r_internal", 0.1f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledVoltageSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ControlledCurrentSource") {
            ControlledCurrentSource<JitProvider> comp;
            
            comp.gain = consume_float_optional("gain", 1.0f);
            comp.min_i = consume_float_optional("min_i", 0.0f);
            comp.max_i = consume_float_optional("max_i", 100.0f);
            comp.g_shunt = consume_float_optional("g_shunt", 0.001f);
            comp.pre_load();
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledCurrentSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VariableConductance") {
            VariableConductance<JitProvider> comp;
            
            comp.g_min = consume_float_optional("g_min", 0.001f);
            comp.g_max = consume_float_optional("g_max", 10.0f);
            setup_ports(comp);
            validate_all_params_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VariableConductance<JitProvider>>(result.devices[dev.name]));
        }
    }

    // Deduplicate fixed_signals (RefNode may have been added above and in the loop above)
    std::sort(result.fixed_signals.begin(), result.fixed_signals.end());
    result.fixed_signals.erase(
        std::unique(result.fixed_signals.begin(), result.fixed_signals.end()),
        result.fixed_signals.end());

    // Phase 3.1: One-source-per-wire validation
    // Check that each electrical signal has at most one active voltage source writing to it.
    // Active source components (these conflict with each other):
    // - Battery: v_out
    // - Generator: v_out
    // - GS24: v_out
    // - RU19A: v_bus (and v_start for starting circuit)
    // - ControlledVoltageSource: v_pos
    // - ControlledCurrentSource: v_pos
    // 
    // RefNode (ground/reference) is NOT considered an active source for this check
    // as it defines the reference point (0V) rather than actively driving voltage.
    
    struct WriterPort {
        std::string component_name;
        std::string port_name;
    };
    
    // Map signal index -> first writer found (for conflict detection)
    std::unordered_map<uint32_t, WriterPort> signal_writers;
    
    // Helper to check and register a writer port
    auto register_writer = [&](const std::string& comp_name, const std::string& port_name) {
        const std::string full_port = comp_name + "." + port_name;
        auto it_signal = result.port_to_signal.find(full_port);
        if (it_signal != result.port_to_signal.end()) {
            const uint32_t signal_idx = it_signal->second;
            auto it_writer = signal_writers.find(signal_idx);
            if (it_writer != signal_writers.end()) {
                // Conflict! Two different active sources on same signal
                std::string msg = "Multiple voltage sources on same wire: ";
                msg += it_writer->second.component_name + "." + it_writer->second.port_name;
                msg += " and " + comp_name + "." + port_name;
                msg += " both write to signal " + std::to_string(signal_idx);
                throw std::runtime_error(msg);
            }
            signal_writers[signal_idx] = {comp_name, port_name};
        }
    };
    
    // Check all migrated active source components (excluding RefNode)
    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }
        
        // Active voltage sources - these CANNOT share a wire
        if (dev.classname == "Battery") {
            register_writer(dev.name, "v_out");
        }
        else if (dev.classname == "Generator") {
            register_writer(dev.name, "v_out");
        }
        else if (dev.classname == "GS24") {
            register_writer(dev.name, "v_out");
        }
        else if (dev.classname == "RU19A") {
            register_writer(dev.name, "v_bus");
            // v_start is also a writer output in the starting circuit
            register_writer(dev.name, "v_start");
        }
        else if (dev.classname == "ControlledVoltageSource") {
            register_writer(dev.name, "v_pos");
        }
        else if (dev.classname == "ControlledCurrentSource") {
            register_writer(dev.name, "v_pos");
        }
        // Note: RefNode is intentionally NOT registered as an active source
        // because it defines the reference (0V) rather than driving voltage
    }

    // Phase 3.2: Topological ordering of consumers (writer -> reader)
    // Sources already run before consumers. Here we order only consumer bucket.
    if (!consumer_device_names.empty()) {
        auto output_ports_for = [](const std::string& classname) -> std::unordered_set<std::string> {
            if (classname == "Battery") return {"v_out"};
            if (classname == "Generator") return {"v_out"};
            if (classname == "RefNode") return {"v"};
            if (classname == "GS24") return {"v_out"};
            if (classname == "RU19A") return {"v_bus", "v_start"};
            if (classname == "ControlledVoltageSource") return {"v_pos", "v_neg"};
            if (classname == "ControlledCurrentSource") return {"v_pos", "v_neg"};

            if (classname == "Switch" || classname == "Relay" || classname == "HoldButton" ||
                classname == "Resistor" || classname == "Bus" || classname == "BlueprintInput" ||
                classname == "BlueprintOutput" || classname == "AGK47" || classname == "DMR400" ||
                classname == "InertiaNode" || classname == "Spring" || classname == "TempSensor" ||
                classname == "Radiator" || classname == "SolenoidValve" || classname == "ElectricHeater" ||
                classname == "ElectricPump" || classname == "Transformer" || classname == "Inverter" ||
                classname == "VoltageSense" || classname == "VariableConductance") {
                return {"v_out", "out", "output", "state", "tripped", "temp",
                        "heat_out", "pressure_out", "rpm_out", "t4_out", "ac_out",
                        "secondary", "i_out", "force_out", "flow_out", "level_out"};
            }

            return {"o", "out", "output", "brightness", "state", "rpm_out",
                    "t4_out", "k_mod", "heat_out", "pressure_out", "fuel_out",
                    "i_out", "temp_out", "level_out", "force_out", "flow_out"};
        };

        std::unordered_map<std::string, const DeviceInstance*> device_by_name;
        device_by_name.reserve(devices.size());
        for (const auto& dev : devices) {
            device_by_name[dev.name] = &dev;
        }

        struct IOSet {
            std::unordered_set<uint32_t> reads;
            std::unordered_set<uint32_t> writes;
        };
        std::unordered_map<std::string, IOSet> io_by_consumer;
        io_by_consumer.reserve(consumer_device_names.size());

        for (const auto& name : consumer_device_names) {
            auto it_dev = device_by_name.find(name);
            if (it_dev == device_by_name.end() || it_dev->second == nullptr) {
                continue;
            }

            const DeviceInstance& dev = *it_dev->second;
            const auto output_ports = output_ports_for(dev.classname);
            auto& io = io_by_consumer[name];

            for (const auto& [port_name, port] : dev.ports) {
                (void)port;
                const std::string full_port = dev.name + "." + port_name;
                auto it_sig = result.port_to_signal.find(full_port);
                if (it_sig == result.port_to_signal.end()) {
                    continue;
                }

                if (output_ports.find(port_name) != output_ports.end()) {
                    io.writes.insert(it_sig->second);
                }
                else {
                    io.reads.insert(it_sig->second);
                }
            }
        }

        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_map<std::string, uint32_t> indegree;
        adj.reserve(consumer_device_names.size());
        indegree.reserve(consumer_device_names.size());

        for (const auto& name : consumer_device_names) {
            indegree[name] = 0;
        }

        std::unordered_map<uint32_t, std::vector<std::string>> writers_by_signal;
        writers_by_signal.reserve(result.signal_count);
        for (const auto& name : consumer_device_names) {
            auto it_io = io_by_consumer.find(name);
            if (it_io == io_by_consumer.end()) {
                continue;
            }
            for (uint32_t sig : it_io->second.writes) {
                writers_by_signal[sig].push_back(name);
            }
        }

        std::unordered_set<std::string> seen_edges;
        for (const auto& reader_name : consumer_device_names) {
            auto it_io = io_by_consumer.find(reader_name);
            if (it_io == io_by_consumer.end()) {
                continue;
            }
            for (uint32_t sig : it_io->second.reads) {
                auto it_writers = writers_by_signal.find(sig);
                if (it_writers == writers_by_signal.end()) {
                    continue;
                }
                for (const auto& writer_name : it_writers->second) {
                    if (writer_name == reader_name) {
                        continue;
                    }
                    const std::string edge = writer_name + "->" + reader_name;
                    if (seen_edges.insert(edge).second) {
                        adj[writer_name].push_back(reader_name);
                        indegree[reader_name]++;
                    }
                }
            }
        }

        std::queue<std::string> ready;
        for (const auto& name : consumer_device_names) {
            if (indegree[name] == 0) {
                ready.push(name);
            }
        }

        std::vector<std::string> sorted;
        sorted.reserve(consumer_device_names.size());
        while (!ready.empty()) {
            const std::string current = ready.front();
            ready.pop();
            sorted.push_back(current);

            auto it_adj = adj.find(current);
            if (it_adj == adj.end()) {
                continue;
            }
            for (const auto& next : it_adj->second) {
                if (--indegree[next] == 0) {
                    ready.push(next);
                }
            }
        }

        if (sorted.size() < consumer_device_names.size()) {
            spdlog::warn("[build] Consumer dependency cycle detected; falling back to one-frame delay ordering for cycle edges");
            std::unordered_set<std::string> in_sorted(sorted.begin(), sorted.end());
            for (const auto& name : consumer_device_names) {
                if (in_sorted.find(name) == in_sorted.end()) {
                    sorted.push_back(name);
                }
            }
        }

        result.scheduler.clear_consumers();
        for (const auto& name : sorted) {
            auto it_var = result.devices.find(name);
            if (it_var == result.devices.end()) {
                continue;
            }
            std::visit([&](auto& comp) {
                result.scheduler.add_consumer(&comp);
            }, it_var->second);
        }
    }

    return result;
}
