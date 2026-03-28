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
#include <unordered_set>
#include <spdlog/spdlog.h>
#include "../parse_number.h"

namespace {
    // Port setup is handled inline via the setup_ports lambda in build_systems_dev
}

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    BuildResult result{};

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

        // Check if this is a migrated component type
        bool is_source = false;
        bool is_migrated = false;
        
        if (dev.classname == "Battery") {
            is_migrated = true;
            is_source = true;
        } else if (dev.classname == "Generator") {
            is_migrated = true;
            is_source = true;
        } else if (dev.classname == "RefNode") {
            is_migrated = true;
            is_source = true;
        }         else if (dev.classname == "Switch" || dev.classname == "Relay" ||
                   dev.classname == "HoldButton" || dev.classname == "Load" ||
                   dev.classname == "Bus" || dev.classname == "BlueprintInput" ||
                   dev.classname == "BlueprintOutput" ||
                   dev.classname == "Comparator" || dev.classname == "CurrentSense" ||
                   dev.classname == "AZS" || dev.classname == "Resistor" ||
                   dev.classname == "Voltmeter" || dev.classname == "IndicatorLight" ||
                   dev.classname == "Add" || dev.classname == "Subtract" ||
                   dev.classname == "Multiply" || dev.classname == "Divide" ||
                   dev.classname == "AND" || dev.classname == "OR" ||
                   dev.classname == "XOR" || dev.classname == "NOT" ||
                   dev.classname == "NAND" || dev.classname == "Min" ||
                   dev.classname == "Max" || dev.classname == "MaxSelector" || dev.classname == "Clamp" ||
                   dev.classname == "PID" || dev.classname == "PI" ||
                   dev.classname == "PD" || dev.classname == "P" ||
                   dev.classname == "Integrator" || dev.classname == "SampleHold" ||
                   dev.classname == "TimeDelay" || dev.classname == "Monostable" ||
                   dev.classname == "SlewRate" || dev.classname == "AsymSlewRate" ||
                   dev.classname == "FastTMO" || dev.classname == "AsymTMO" ||
                   dev.classname == "Normalize" || dev.classname == "LUT" ||
                   dev.classname == "Greater" || dev.classname == "Lesser" ||
                   dev.classname == "GreaterEq" || dev.classname == "LesserEq" ||
                   dev.classname == "Any_V_to_Bool" || dev.classname == "Positive_V_to_Bool" ||
                   dev.classname == "LerpNode" || dev.classname == "Slider" ||
                   dev.classname == "Splitter" || dev.classname == "Merger" ||
                   // Phase 2 Slice 6: Additional non-controlled components
                   dev.classname == "AGK47" || dev.classname == "DMR400" ||
                   dev.classname == "ElectricHeater" || dev.classname == "ElectricPump" ||
                   dev.classname == "FuelTank" || dev.classname == "GidroAccumulator" ||
                   dev.classname == "GS24" || dev.classname == "Gyroscope" ||
                   dev.classname == "HighPowerLoad" || dev.classname == "InertiaNode" ||
                   dev.classname == "Inverter" || dev.classname == "Radiator" ||
                   dev.classname == "RU19A" || dev.classname == "RUG82" ||
                   dev.classname == "SolenoidValve" || dev.classname == "Spring" ||
                   dev.classname == "TempSensor" || dev.classname == "Transformer" ||
                   dev.classname == "VoltageSense" ||
                   // Phase 2 Slice 7: Controlled source / conductance components
                   dev.classname == "ControlledVoltageSource" ||
                   dev.classname == "ControlledCurrentSource" ||
                   dev.classname == "VariableConductance") {
            is_migrated = true;
            is_source = false;
        }

        if (!is_migrated) {
            continue;
        }

        if (!is_source) {
            consumer_device_names.push_back(dev.name);
        }

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
            if (auto it = dev.params.find("v_nominal"); it != dev.params.end()) {
                comp.v_nominal = locale_safe::parse_float_or(it->second, 28.0f);
            }
            if (auto it = dev.params.find("internal_r"); it != dev.params.end()) {
                comp.internal_r = locale_safe::parse_float_or(it->second, 0.01f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<Battery<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Generator") {
            Generator<JitProvider> comp;
            
            if (auto it = dev.params.find("v_nominal"); it != dev.params.end()) {
                comp.v_nominal = locale_safe::parse_float_or(it->second, 28.5f);
            }
            if (auto it = dev.params.find("internal_r"); it != dev.params.end()) {
                comp.internal_r = locale_safe::parse_float_or(it->second, 0.005f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<Generator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RefNode") {
            RefNode<JitProvider> comp;
            
            if (auto it = dev.params.find("value"); it != dev.params.end()) {
                comp.value = locale_safe::parse_float_or(it->second, 0.0f);
            }
            setup_ports(comp);
            
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
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Switch<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Relay") {
            Relay<JitProvider> comp;
            
            if (auto it = dev.params.find("hold_threshold"); it != dev.params.end()) {
                comp.hold_threshold = locale_safe::parse_float_or(it->second, 0.5f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Relay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HoldButton") {
            HoldButton<JitProvider> comp;
            
            if (auto it = dev.params.find("idle"); it != dev.params.end()) {
                comp.idle = locale_safe::parse_float_or(it->second, 0.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HoldButton<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Load") {
            Load<JitProvider> comp;
            
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 0.1f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Load<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Bus") {
            Bus<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Bus<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintInput") {
            BlueprintInput<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintInput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintOutput") {
            BlueprintOutput<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintOutput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Comparator") {
            Comparator<JitProvider> comp;
            
            if (auto it = dev.params.find("Von"); it != dev.params.end()) {
                comp.Von = locale_safe::parse_float_or(it->second, 5.0f);
            }
            if (auto it = dev.params.find("Voff"); it != dev.params.end()) {
                comp.Voff = locale_safe::parse_float_or(it->second, 2.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Comparator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "CurrentSense") {
            CurrentSense<JitProvider> comp;
            
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<CurrentSense<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AZS") {
            AZS<JitProvider> comp;
            
            if (auto it = dev.params.find("closed"); it != dev.params.end()) {
                comp.closed = (it->second == "true" || it->second == "1");
            }
            if (auto it = dev.params.find("i_nominal"); it != dev.params.end()) {
                comp.i_nominal = locale_safe::parse_float_or(it->second, 20.0f);
            }
            if (auto it = dev.params.find("k_cool"); it != dev.params.end()) {
                comp.k_cool = locale_safe::parse_float_or(it->second, 1.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AZS<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Resistor") {
            Resistor<JitProvider> comp;
            
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 0.1f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Resistor<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Voltmeter") {
            Voltmeter<JitProvider> comp;
            
            if (auto it = dev.params.find("min"); it != dev.params.end()) {
                comp.min = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max"); it != dev.params.end()) {
                comp.max = locale_safe::parse_float_or(it->second, 28.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Voltmeter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "IndicatorLight") {
            IndicatorLight<JitProvider> comp;
            
            if (auto it = dev.params.find("max_brightness"); it != dev.params.end()) {
                comp.max_brightness = locale_safe::parse_float_or(it->second, 100.0f);
            }
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("rated_voltage"); it != dev.params.end()) {
                comp.rated_voltage = locale_safe::parse_float_or(it->second, 28.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<IndicatorLight<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 3: Logical/math components
        else if (dev.classname == "Add") {
            Add<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Add<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Subtract") {
            Subtract<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Subtract<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Multiply") {
            Multiply<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Multiply<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Divide") {
            Divide<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Divide<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AND") {
            AND<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "OR") {
            OR<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<OR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "XOR") {
            XOR<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<XOR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NOT") {
            NOT<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NOT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NAND") {
            NAND<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NAND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Min") {
            Min<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Min<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Max" || dev.classname == "MaxSelector") {
            Max<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Max<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Clamp") {
            Clamp<JitProvider> comp;
            
            if (auto it = dev.params.find("min"); it != dev.params.end()) {
                comp.min = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max"); it != dev.params.end()) {
                comp.max = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Clamp<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 4: Control/filter components
        else if (dev.classname == "PID") {
            PID<JitProvider> comp;
            
            if (auto it = dev.params.find("kp"); it != dev.params.end()) {
                comp.Kp = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("ki"); it != dev.params.end()) {
                comp.Ki = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("kd"); it != dev.params.end()) {
                comp.Kd = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("out_min"); it != dev.params.end()) {
                comp.output_min = locale_safe::parse_float_or(it->second, -1000.0f);
            }
            if (auto it = dev.params.find("out_max"); it != dev.params.end()) {
                comp.output_max = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PID<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PI") {
            PI<JitProvider> comp;
            
            if (auto it = dev.params.find("kp"); it != dev.params.end()) {
                comp.Kp = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("ki"); it != dev.params.end()) {
                comp.Ki = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("out_min"); it != dev.params.end()) {
                comp.output_min = locale_safe::parse_float_or(it->second, -1000.0f);
            }
            if (auto it = dev.params.find("out_max"); it != dev.params.end()) {
                comp.output_max = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PI<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PD") {
            PD<JitProvider> comp;
            
            if (auto it = dev.params.find("kp"); it != dev.params.end()) {
                comp.Kp = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("kd"); it != dev.params.end()) {
                comp.Kd = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("out_min"); it != dev.params.end()) {
                comp.output_min = locale_safe::parse_float_or(it->second, -1000.0f);
            }
            if (auto it = dev.params.find("out_max"); it != dev.params.end()) {
                comp.output_max = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PD<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "P") {
            P<JitProvider> comp;
            
            if (auto it = dev.params.find("kp"); it != dev.params.end()) {
                comp.Kp = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("out_min"); it != dev.params.end()) {
                comp.output_min = locale_safe::parse_float_or(it->second, -1000.0f);
            }
            if (auto it = dev.params.find("out_max"); it != dev.params.end()) {
                comp.output_max = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<P<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Integrator") {
            Integrator<JitProvider> comp;
            
            if (auto it = dev.params.find("k"); it != dev.params.end()) {
                comp.gain = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Integrator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SampleHold") {
            SampleHold<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SampleHold<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "TimeDelay") {
            TimeDelay<JitProvider> comp;
            
            if (auto it = dev.params.find("delay_on"); it != dev.params.end()) {
                comp.delay_on = locale_safe::parse_float_or(it->second, 0.5f);
            }
            if (auto it = dev.params.find("delay_off"); it != dev.params.end()) {
                comp.delay_off = locale_safe::parse_float_or(it->second, 0.1f);
            }
            // Fallback: single "delay" param sets both timers equally
            if (auto it = dev.params.find("delay"); it != dev.params.end()) {
                float d = locale_safe::parse_float_or(it->second, 0.5f);
                if (dev.params.find("delay_on") == dev.params.end()) comp.delay_on = d;
                if (dev.params.find("delay_off") == dev.params.end()) comp.delay_off = d;
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TimeDelay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Monostable") {
            Monostable<JitProvider> comp;
            
            if (auto it = dev.params.find("duration"); it != dev.params.end()) {
                comp.duration = locale_safe::parse_float_or(it->second, 30.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Monostable<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SlewRate") {
            SlewRate<JitProvider> comp;
            
            if (auto it = dev.params.find("rate"); it != dev.params.end()) {
                comp.max_rate = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymSlewRate") {
            AsymSlewRate<JitProvider> comp;
            
            if (auto it = dev.params.find("up_rate"); it != dev.params.end()) {
                comp.rate_up = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("down_rate"); it != dev.params.end()) {
                comp.rate_down = locale_safe::parse_float_or(it->second, 0.5f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymSlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FastTMO") {
            FastTMO<JitProvider> comp;
            
            if (auto it = dev.params.find("tau"); it != dev.params.end()) {
                comp.tau = locale_safe::parse_float_or(it->second, 0.1f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FastTMO<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymTMO") {
            AsymTMO<JitProvider> comp;
            
            if (auto it = dev.params.find("tau_up"); it != dev.params.end()) {
                comp.tau_up = locale_safe::parse_float_or(it->second, 0.1f);
            }
            if (auto it = dev.params.find("tau_down"); it != dev.params.end()) {
                comp.tau_down = locale_safe::parse_float_or(it->second, 0.5f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymTMO<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 5: Signal-shaping / utility logical components
        else if (dev.classname == "Normalize") {
            Normalize<JitProvider> comp;
            
            if (auto it = dev.params.find("min"); it != dev.params.end()) {
                comp.min = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max"); it != dev.params.end()) {
                comp.max = locale_safe::parse_float_or(it->second, 100.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Normalize<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LUT") {
            LUT<JitProvider> comp;
            
            // Parse table data from "table" param into arena
            if (auto it = dev.params.find("table"); it != dev.params.end()) {
                std::vector<float> keys, vals;
                if (LUT<JitProvider>::parse_table(it->second, keys, vals)) {
                    comp.table_offset = static_cast<uint32_t>(result.lut_keys.size());
                    comp.table_size = static_cast<uint16_t>(keys.size());
                    result.lut_keys.insert(result.lut_keys.end(), keys.begin(), keys.end());
                    result.lut_values.insert(result.lut_values.end(), vals.begin(), vals.end());
                }
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LUT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Greater") {
            Greater<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Greater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Lesser") {
            Lesser<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Lesser<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GreaterEq") {
            GreaterEq<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GreaterEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LesserEq") {
            LesserEq<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LesserEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Any_V_to_Bool") {
            Any_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Any_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Positive_V_to_Bool") {
            Positive_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Positive_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LerpNode") {
            LerpNode<JitProvider> comp;
            
            if (auto it = dev.params.find("factor"); it != dev.params.end()) {
                comp.factor = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("deadzone"); it != dev.params.end()) {
                comp.deadzone = locale_safe::parse_float_or(it->second, 0.001f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LerpNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Slider") {
            Slider<JitProvider> comp;
            
            if (auto it = dev.params.find("min"); it != dev.params.end()) {
                comp.min = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max"); it != dev.params.end()) {
                comp.max = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Slider<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Splitter") {
            Splitter<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Splitter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Merger") {
            Merger<JitProvider> comp;
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Merger<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 6: Additional non-controlled components
        else if (dev.classname == "AGK47") {
            AGK47<JitProvider> comp;
            
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 0.001f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AGK47<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "DMR400") {
            DMR400<JitProvider> comp;
            
            if (auto it = dev.params.find("connect_threshold"); it != dev.params.end()) {
                comp.connect_threshold = locale_safe::parse_float_or(it->second, 2.0f);
            }
            if (auto it = dev.params.find("disconnect_threshold"); it != dev.params.end()) {
                comp.disconnect_threshold = locale_safe::parse_float_or(it->second, 10.0f);
            }
            if (auto it = dev.params.find("min_voltage_to_close"); it != dev.params.end()) {
                comp.min_voltage_to_close = locale_safe::parse_float_or(it->second, 20.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<DMR400<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricHeater") {
            ElectricHeater<JitProvider> comp;
            
            if (auto it = dev.params.find("max_power"); it != dev.params.end()) {
                comp.max_power = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            if (auto it = dev.params.find("efficiency"); it != dev.params.end()) {
                comp.efficiency = locale_safe::parse_float_or(it->second, 0.9f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricHeater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricPump") {
            ElectricPump<JitProvider> comp;
            
            if (auto it = dev.params.find("max_pressure"); it != dev.params.end()) {
                comp.max_pressure = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricPump<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FuelTank") {
            FuelTank<JitProvider> comp;
            
            if (auto it = dev.params.find("capacity"); it != dev.params.end()) {
                comp.capacity = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            if (auto it = dev.params.find("level"); it != dev.params.end()) {
                comp.level = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            if (auto it = dev.params.find("density"); it != dev.params.end()) {
                comp.density = locale_safe::parse_float_or(it->second, 0.78f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FuelTank<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GidroAccumulator") {
            GidroAccumulator<JitProvider> comp;
            
            if (auto it = dev.params.find("precharge_pressure"); it != dev.params.end()) {
                comp.precharge_pressure = locale_safe::parse_float_or(it->second, 50.0f);
            }
            if (auto it = dev.params.find("volume"); it != dev.params.end()) {
                comp.volume = locale_safe::parse_float_or(it->second, 10.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GidroAccumulator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GS24") {
            GS24<JitProvider> comp;
            
            if (auto it = dev.params.find("target_rpm"); it != dev.params.end()) {
                comp.target_rpm = locale_safe::parse_float_or(it->second, 15000.0f);
            }
            if (auto it = dev.params.find("r_internal"); it != dev.params.end()) {
                comp.r_internal = locale_safe::parse_float_or(it->second, 0.025f);
            }
            if (auto it = dev.params.find("r_norton"); it != dev.params.end()) {
                comp.r_norton = locale_safe::parse_float_or(it->second, 0.08f);
            }
            if (auto it = dev.params.find("k_motor"); it != dev.params.end()) {
                comp.k_motor = locale_safe::parse_float_or(it->second, 0.5f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GS24<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Gyroscope") {
            Gyroscope<JitProvider> comp;
            
            if (auto it = dev.params.find("conductance"); it != dev.params.end()) {
                comp.conductance = locale_safe::parse_float_or(it->second, 0.001f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Gyroscope<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HighPowerLoad") {
            HighPowerLoad<JitProvider> comp;
            
            if (auto it = dev.params.find("power_draw"); it != dev.params.end()) {
                comp.power_draw = locale_safe::parse_float_or(it->second, 500.0f);
            }
            if (auto it = dev.params.find("min_voltage_diff"); it != dev.params.end()) {
                comp.min_voltage_diff = locale_safe::parse_float_or(it->second, 0.01f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HighPowerLoad<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "InertiaNode") {
            InertiaNode<JitProvider> comp;
            
            if (auto it = dev.params.find("mass"); it != dev.params.end()) {
                comp.mass = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("damping"); it != dev.params.end()) {
                comp.damping = locale_safe::parse_float_or(it->second, 0.5f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<InertiaNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Inverter") {
            Inverter<JitProvider> comp;
            
            if (auto it = dev.params.find("efficiency"); it != dev.params.end()) {
                comp.efficiency = locale_safe::parse_float_or(it->second, 0.95f);
            }
            if (auto it = dev.params.find("frequency"); it != dev.params.end()) {
                comp.frequency = locale_safe::parse_float_or(it->second, 400.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Inverter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Radiator") {
            Radiator<JitProvider> comp;
            
            if (auto it = dev.params.find("cooling_capacity"); it != dev.params.end()) {
                comp.cooling_capacity = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Radiator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RU19A") {
            RU19A<JitProvider> comp;
            
            if (auto it = dev.params.find("target_rpm"); it != dev.params.end()) {
                comp.target_rpm = locale_safe::parse_float_or(it->second, 16000.0f);
            }
            if (auto it = dev.params.find("auto_start"); it != dev.params.end()) {
                comp.auto_start = (it->second == "true" || it->second == "1");
            }
            if (auto it = dev.params.find("t4_target"); it != dev.params.end()) {
                comp.t4_target = locale_safe::parse_float_or(it->second, 400.0f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RU19A<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RUG82") {
            RUG82<JitProvider> comp;
            
            if (auto it = dev.params.find("v_target"); it != dev.params.end()) {
                comp.v_target = locale_safe::parse_float_or(it->second, 28.5f);
            }
            if (auto it = dev.params.find("kp"); it != dev.params.end()) {
                comp.kp = locale_safe::parse_float_or(it->second, 2.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RUG82<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SolenoidValve") {
            SolenoidValve<JitProvider> comp;
            
            if (auto it = dev.params.find("normally_closed"); it != dev.params.end()) {
                comp.normally_closed = (it->second == "true" || it->second == "1");
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SolenoidValve<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Spring") {
            Spring<JitProvider> comp;
            
            if (auto it = dev.params.find("k"); it != dev.params.end()) {
                comp.k = locale_safe::parse_float_or(it->second, 1000.0f);
            }
            if (auto it = dev.params.find("c"); it != dev.params.end()) {
                comp.c = locale_safe::parse_float_or(it->second, 10.0f);
            }
            if (auto it = dev.params.find("rest_length"); it != dev.params.end()) {
                comp.rest_length = locale_safe::parse_float_or(it->second, 0.1f);
            }
            if (auto it = dev.params.find("compression_only"); it != dev.params.end()) {
                comp.compression_only = (it->second == "true" || it->second == "1");
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Spring<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "TempSensor") {
            TempSensor<JitProvider> comp;
            
            if (auto it = dev.params.find("sensitivity"); it != dev.params.end()) {
                comp.sensitivity = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TempSensor<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Transformer") {
            Transformer<JitProvider> comp;
            
            if (auto it = dev.params.find("ratio"); it != dev.params.end()) {
                comp.ratio = locale_safe::parse_float_or(it->second, 1.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Transformer<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VoltageSense") {
            VoltageSense<JitProvider> comp;
            
            if (auto it = dev.params.find("gain"); it != dev.params.end()) {
                comp.gain = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("offset"); it != dev.params.end()) {
                comp.offset = locale_safe::parse_float_or(it->second, 0.0f);
            }
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VoltageSense<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 7: Controlled source / conductance components
        else if (dev.classname == "ControlledVoltageSource") {
            ControlledVoltageSource<JitProvider> comp;
            
            if (auto it = dev.params.find("gain"); it != dev.params.end()) {
                comp.gain = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("offset"); it != dev.params.end()) {
                comp.offset = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("min_v"); it != dev.params.end()) {
                comp.min_v = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max_v"); it != dev.params.end()) {
                comp.max_v = locale_safe::parse_float_or(it->second, 30.0f);
            }
            if (auto it = dev.params.find("r_internal"); it != dev.params.end()) {
                comp.r_internal = locale_safe::parse_float_or(it->second, 0.1f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledVoltageSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ControlledCurrentSource") {
            ControlledCurrentSource<JitProvider> comp;
            
            if (auto it = dev.params.find("gain"); it != dev.params.end()) {
                comp.gain = locale_safe::parse_float_or(it->second, 1.0f);
            }
            if (auto it = dev.params.find("min_i"); it != dev.params.end()) {
                comp.min_i = locale_safe::parse_float_or(it->second, 0.0f);
            }
            if (auto it = dev.params.find("max_i"); it != dev.params.end()) {
                comp.max_i = locale_safe::parse_float_or(it->second, 100.0f);
            }
            if (auto it = dev.params.find("g_shunt"); it != dev.params.end()) {
                comp.g_shunt = locale_safe::parse_float_or(it->second, 0.001f);
            }
            comp.pre_load();
            setup_ports(comp);
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledCurrentSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VariableConductance") {
            VariableConductance<JitProvider> comp;
            
            if (auto it = dev.params.find("g_min"); it != dev.params.end()) {
                comp.g_min = locale_safe::parse_float_or(it->second, 0.001f);
            }
            if (auto it = dev.params.find("g_max"); it != dev.params.end()) {
                comp.g_max = locale_safe::parse_float_or(it->second, 10.0f);
            }
            setup_ports(comp);
            
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
