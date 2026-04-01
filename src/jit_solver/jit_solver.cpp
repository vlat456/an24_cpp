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
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"

#include <algorithm>
#include <map>
#include <queue>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>
#include "../parse_number.h"

namespace {
std::string metadata_classname_for(std::string_view classname) {
    return std::string(classname);
}

bool is_scheduler_source_component_class(std::string_view classname) {
    return is_scheduler_source_component(metadata_classname_for(classname));
}

/// Returns true for components that are solver-owned for electrical propagation.
/// These components run inside the electrical solver, NOT the push scheduler.
/// RefNode remains scheduled as a source (it writes constant reference values).
/// Guard: prevents accidental reintroduction of push scheduling for these classes.
bool is_solver_owned_electrical_propagator(std::string_view classname) {
    return classname == "Battery" ||
           classname == "Generator" ||
           classname == "Resistor" ||
           classname == "ElectricalConductance" ||
           classname == "ElectricalSource" ||
           classname == "ControlledVoltageSource";
}

std::vector<std::string> active_source_writer_ports_for(std::string_view classname) {
    return get_source_writer_ports(
        metadata_classname_for(classname),
        static_cast<uint8_t>(Domain::Electrical));
}

std::unordered_set<std::string> output_ports_for_class(std::string_view classname) {
    auto outputs = get_output_ports(metadata_classname_for(classname));
    return std::unordered_set<std::string>(outputs.begin(), outputs.end());
}

bool parse_bool_param_value(const std::string& value) {
    return value == "true" || value == "1";
}

class ParamReader {
public:
    ParamReader(const std::unordered_map<std::string, std::string>& params, const DeviceInstance& dev)
        : params_(params), dev_(dev) {}

    float consume_float_optional(const std::string& key, float default_val) {
        consumed_params_.insert(key);
        auto it = params_.find(key);
        if (it != params_.end()) {
            return locale_safe::parse_float_or(it->second, default_val);
        }
        return default_val;
    }

    bool consume_bool_optional(const std::string& key, bool default_val) {
        consumed_params_.insert(key);
        auto it = params_.find(key);
        if (it != params_.end()) {
            return parse_bool_param_value(it->second);
        }
        return default_val;
    }

    std::string consume_string_optional(const std::string& key, const std::string& default_val) {
        consumed_params_.insert(key);
        auto it = params_.find(key);
        if (it != params_.end()) {
            return it->second;
        }
        return default_val;
    }

    float consume_float_required(const std::string& key) {
        consumed_params_.insert(key);
        return locale_safe::parse_float_or(get_required(key), 0.0f);
    }

    bool consume_bool_required(const std::string& key) {
        consumed_params_.insert(key);
        return parse_bool_param_value(get_required(key));
    }

    void validate_all_consumed() const {
        for (const auto& [key, val] : params_) {
            (void)val;
            if (consumed_params_.find(key) == consumed_params_.end()) {
                throw std::runtime_error("Unknown/unconsumed parameter '" + key +
                    "' for component '" + dev_.name + "' (classname: " + dev_.classname + ")");
            }
        }
    }

private:
    const std::string& get_required(const std::string& key) const {
        auto it = params_.find(key);
        if (it == params_.end()) {
            std::string available;
            for (const auto& [k, v] : params_) {
                (void)v;
                if (!available.empty()) {
                    available += ", ";
                }
                available += k;
            }
            throw std::runtime_error("Missing required parameter '" + key +
                "' for component '" + dev_.name + "' (classname: " + dev_.classname +
                "). Available keys: " + available);
        }
        return it->second;
    }

    const std::unordered_map<std::string, std::string>& params_;
    const DeviceInstance& dev_;
    std::unordered_set<std::string> consumed_params_;
};
} // namespace

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
        } else {
            if (it_from == port_to_idx.end()) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected to '{}')", from, to);
            }
            if (it_to == port_to_idx.end()) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected from '{}')", to, from);
            }
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

        if (!has_component_metadata(metadata_classname_for(dev.classname))) {
            throw std::runtime_error("Missing generated port metadata for component class '" + dev.classname + "'");
        }

        bool is_source = is_scheduler_source_component_class(dev.classname);
        bool is_solver_owned_electrical = is_solver_owned_electrical_propagator(dev.classname);

        // Guard: solver-owned electrical propagators must NOT be added to scheduler
        // as consumers. They are handled by the electrical solver instead.
        if (!is_source && !is_solver_owned_electrical) {
            consumer_device_names.push_back(dev.name);
        }

        ParamReader param_reader(dev.params, dev);

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
            comp.v_nominal = param_reader.consume_float_optional("v_nominal", 28.0f);
            comp.internal_r = param_reader.consume_float_optional("internal_r", 0.01f);
            // capacity and charge are stored but not used in current battery model
            comp.capacity = param_reader.consume_float_optional("capacity", 1000.0f);
            comp.charge = param_reader.consume_float_optional("charge", 1000.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // NOTE: Battery is NOT scheduled for push electrical propagation.
            // Electrical propagation is now handled by the electrical solver (Batch 4/5).
        }
        else if (dev.classname == "Generator") {
            Generator<JitProvider> comp;
            
            comp.v_nominal = param_reader.consume_float_optional("v_nominal", 28.5f);
            comp.internal_r = param_reader.consume_float_optional("internal_r", 0.005f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // NOTE: Generator is NOT scheduled for push electrical propagation.
            // Electrical propagation is now handled by the electrical solver (Batch 4/5).
        }
        else if (dev.classname == "RefNode") {
            RefNode<JitProvider> comp;
            
            comp.value = param_reader.consume_float_optional("value", 0.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // RefNode is a source: it writes its fixed value into the signal array
            // every frame so downstream consumers see the correct reference.
            result.scheduler.add_source(&std::get<RefNode<JitProvider>>(result.devices[dev.name]));
            
            // Also track as fixed signal
            const std::string key = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(key);
            if (it_sig != result.port_to_signal.end()) {
                result.fixed_signals.push_back(it_sig->second);
            }
        }
        else if (dev.classname == "Value") {
            Value<JitProvider> comp;

            comp.value = param_reader.consume_float_optional("value", 0.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();

            result.devices[dev.name] = comp;
            // Value is a scheduler source: writes constant to output each frame.
            // Unlike RefNode, Value has NO electrical semantics and is never
            // extracted into the electrical plan.
            result.scheduler.add_source(&std::get<Value<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Switch") {
            Switch<JitProvider> comp;
            
            comp.closed = param_reader.consume_bool_optional("closed", false);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Switch<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Relay") {
            Relay<JitProvider> comp;
            
            comp.closed = param_reader.consume_bool_optional("closed", false);
            comp.hold_threshold = param_reader.consume_float_optional("hold_threshold", 0.5f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Relay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HoldButton") {
            HoldButton<JitProvider> comp;
            
            comp.idle = param_reader.consume_float_optional("idle", 0.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HoldButton<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Load") {
            Load<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Load<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Bus") {
            Bus<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Bus<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintInput") {
            BlueprintInput<JitProvider> comp;
            // Consume metadata params (used by extract_exposed_ports, not by runtime)
            param_reader.consume_string_optional("exposed_direction", "In");
            param_reader.consume_string_optional("exposed_type", "V");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintInput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintOutput") {
            BlueprintOutput<JitProvider> comp;
            // Consume metadata params (used by extract_exposed_ports, not by runtime)
            param_reader.consume_string_optional("exposed_direction", "Out");
            param_reader.consume_string_optional("exposed_type", "V");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintOutput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Comparator") {
            Comparator<JitProvider> comp;
            
            comp.Von = param_reader.consume_float_optional("Von", 5.0f);
            comp.Voff = param_reader.consume_float_optional("Voff", 2.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Comparator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "CurrentSense") {
            CurrentSense<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 1000.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<CurrentSense<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AZS") {
            AZS<JitProvider> comp;
            
            comp.closed = param_reader.consume_bool_optional("closed", false);
            comp.i_nominal = param_reader.consume_float_optional("i_nominal", 20.0f);
            comp.k_cool = param_reader.consume_float_optional("k_cool", 1.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AZS<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Resistor") {
            Resistor<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // NOTE: Resistor is NOT scheduled for push electrical propagation.
            // Electrical propagation is now handled by the electrical solver (Batch 4/5).
        }
        else if (dev.classname == "ElectricalConductance") {
            ElectricalConductance<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // Primitive: solver-owned, not scheduled for push.
        }
        else if (dev.classname == "ElectricalSource") {
            ElectricalSource<JitProvider> comp;
            
            comp.voltage = param_reader.consume_float_optional("voltage", 28.0f);
            comp.resistance = param_reader.consume_float_optional("resistance", 0.01f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // Primitive: solver-owned, not scheduled for push.
        }
        else if (dev.classname == "Voltmeter") {
            Voltmeter<JitProvider> comp;
            
            comp.min = param_reader.consume_float_optional("min", 0.0f);
            comp.max = param_reader.consume_float_optional("max", 28.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Voltmeter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "IndicatorLight") {
            IndicatorLight<JitProvider> comp;
            
            comp.max_brightness = param_reader.consume_float_optional("max_brightness", 100.0f);
            comp.conductance = param_reader.consume_float_optional("conductance", 1.0f);
            comp.rated_voltage = param_reader.consume_float_optional("rated_voltage", 28.0f);
            comp.color = param_reader.consume_string_optional("color", "white");
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<IndicatorLight<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 3: Logical/math components
        else if (dev.classname == "Add") {
            Add<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Add<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Subtract") {
            Subtract<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Subtract<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Multiply") {
            Multiply<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Multiply<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Divide") {
            Divide<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Divide<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AND") {
            AND<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "OR") {
            OR<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<OR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "XOR") {
            XOR<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<XOR<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NOT") {
            NOT<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NOT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "NAND") {
            NAND<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<NAND<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Min") {
            Min<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Min<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Max") {
            Max<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Max<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Clamp") {
            Clamp<JitProvider> comp;
            
            comp.min = param_reader.consume_float_optional("min", 0.0f);
            comp.max = param_reader.consume_float_optional("max", 1.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Clamp<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 4: Control/filter components
        else if (dev.classname == "PID") {
            PID<JitProvider> comp;
            
            comp.Kp = param_reader.consume_float_required("Kp");
            comp.Ki = param_reader.consume_float_required("Ki");
            comp.Kd = param_reader.consume_float_required("Kd");
            comp.output_min = param_reader.consume_float_required("output_min");
            comp.output_max = param_reader.consume_float_required("output_max");
            comp.filter_alpha = param_reader.consume_float_required("filter_alpha");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PID<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PI") {
            PI<JitProvider> comp;
            
            comp.Kp = param_reader.consume_float_required("Kp");
            comp.Ki = param_reader.consume_float_required("Ki");
            comp.output_min = param_reader.consume_float_required("output_min");
            comp.output_max = param_reader.consume_float_required("output_max");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PI<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "PD") {
            PD<JitProvider> comp;
            
            comp.Kp = param_reader.consume_float_required("Kp");
            comp.Kd = param_reader.consume_float_required("Kd");
            comp.filter_alpha = param_reader.consume_float_required("filter_alpha");
            comp.output_min = param_reader.consume_float_required("output_min");
            comp.output_max = param_reader.consume_float_required("output_max");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<PD<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "P") {
            P<JitProvider> comp;
            
            comp.Kp = param_reader.consume_float_required("Kp");
            comp.output_min = param_reader.consume_float_required("output_min");
            comp.output_max = param_reader.consume_float_required("output_max");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<P<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Integrator") {
            Integrator<JitProvider> comp;
            
            comp.gain = param_reader.consume_float_required("gain");
            comp.initial_val = param_reader.consume_float_required("initial_val");
            comp.accumulator = comp.initial_val;
            comp.next_accumulator = comp.initial_val;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Integrator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SampleHold") {
            SampleHold<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
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
                float d = param_reader.consume_float_required("delay");
                comp.delay_on = d;
                comp.delay_off = d;
            }
            if (has_delay_on) {
                comp.delay_on = param_reader.consume_float_required("delay_on");
            }
            if (has_delay_off) {
                comp.delay_off = param_reader.consume_float_required("delay_off");
            }
            // Require at least one of delay/delay_on/delay_off
            if (!has_delay && !has_delay_on && !has_delay_off) {
                throw std::runtime_error("Missing required parameter for TimeDelay '" + dev.name +
                    "': must have 'delay', or 'delay_on'/'delay_off'. Available keys: ");
            }
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TimeDelay<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Monostable") {
            Monostable<JitProvider> comp;
            
            comp.duration = param_reader.consume_float_required("duration");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Monostable<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SlewRate") {
            SlewRate<JitProvider> comp;
            
            comp.max_rate = param_reader.consume_float_required("max_rate");
            comp.deadzone = param_reader.consume_float_required("deadzone");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymSlewRate") {
            AsymSlewRate<JitProvider> comp;
            
            comp.rate_up = param_reader.consume_float_required("rate_up");
            comp.rate_down = param_reader.consume_float_required("rate_down");
            comp.deadzone = param_reader.consume_float_required("deadzone");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymSlewRate<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FastTMO") {
            FastTMO<JitProvider> comp;
            
            comp.tau = param_reader.consume_float_required("tau");
            comp.deadzone = param_reader.consume_float_optional("deadzone", 0.001f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FastTMO<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AsymTMO") {
            AsymTMO<JitProvider> comp;
            
            comp.tau_up = param_reader.consume_float_required("tau_up");
            comp.tau_down = param_reader.consume_float_required("tau_down");
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AsymTMO<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 5: Signal-shaping / utility logical components
        else if (dev.classname == "Normalize") {
            Normalize<JitProvider> comp;
            
            comp.min = param_reader.consume_float_optional("min", 0.0f);
            comp.max = param_reader.consume_float_optional("max", 100.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Normalize<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LUT") {
            LUT<JitProvider> comp;
            
            // Parse table data from "table" param into arena
            if (auto it = dev.params.find("table"); it != dev.params.end()) {
                const std::string table = param_reader.consume_string_optional("table", "");
                std::vector<float> keys, vals;
                if (LUT<JitProvider>::parse_table(table, keys, vals)) {
                    comp.table_offset = static_cast<uint32_t>(result.lut_keys.size());
                    comp.table_size = static_cast<uint16_t>(keys.size());
                    result.lut_keys.insert(result.lut_keys.end(), keys.begin(), keys.end());
                    result.lut_values.insert(result.lut_values.end(), vals.begin(), vals.end());
                }
            }
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LUT<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Greater") {
            Greater<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Greater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Lesser") {
            Lesser<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Lesser<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GreaterEq") {
            GreaterEq<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GreaterEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LesserEq") {
            LesserEq<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LesserEq<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Any_V_to_Bool") {
            Any_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Any_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Positive_V_to_Bool") {
            Positive_V_to_Bool<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Positive_V_to_Bool<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LerpNode") {
            LerpNode<JitProvider> comp;
            
            comp.factor = param_reader.consume_float_required("factor");
            comp.deadzone = param_reader.consume_float_required("deadzone");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<LerpNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Slider") {
            Slider<JitProvider> comp;
            
            comp.min = param_reader.consume_float_optional("min", 0.0f);
            comp.max = param_reader.consume_float_optional("max", 1.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Slider<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Splitter") {
            Splitter<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Splitter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Merger") {
            Merger<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Merger<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 6: Additional non-controlled components
        else if (dev.classname == "AGK47") {
            AGK47<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 0.001f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<AGK47<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "DMR400") {
            DMR400<JitProvider> comp;
            
            comp.connect_threshold = param_reader.consume_float_optional("connect_threshold", 2.0f);
            comp.disconnect_threshold = param_reader.consume_float_optional("disconnect_threshold", 10.0f);
            comp.min_voltage_to_close = param_reader.consume_float_optional("min_voltage_to_close", 20.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<DMR400<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricHeater") {
            ElectricHeater<JitProvider> comp;
            
            comp.max_power = param_reader.consume_float_optional("max_power", 1000.0f);
            comp.efficiency = param_reader.consume_float_optional("efficiency", 0.9f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricHeater<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "ElectricPump") {
            ElectricPump<JitProvider> comp;
            
            comp.max_pressure = param_reader.consume_float_optional("max_pressure", 1000.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ElectricPump<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "FuelTank") {
            FuelTank<JitProvider> comp;
            
            comp.capacity = param_reader.consume_float_optional("capacity", 1000.0f);
            comp.level = param_reader.consume_float_optional("level", 1000.0f);
            comp.density = param_reader.consume_float_optional("density", 0.78f);
            comp.consumption_rate = param_reader.consume_float_optional("consumption_rate", 0.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<FuelTank<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GidroAccumulator") {
            GidroAccumulator<JitProvider> comp;
            
            comp.precharge_pressure = param_reader.consume_float_optional("precharge_pressure", 50.0f);
            comp.volume = param_reader.consume_float_optional("volume", 10.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GidroAccumulator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "GS24") {
            GS24<JitProvider> comp;
            
            comp.v_nominal = param_reader.consume_float_optional("v_nominal", 28.5f);
            comp.target_rpm = param_reader.consume_float_optional("target_rpm", 16000.0f);
            comp.r_internal = param_reader.consume_float_optional("r_internal", 0.025f);
            comp.r_norton = param_reader.consume_float_optional("r_norton", 0.08f);
            comp.rpm_cutoff = param_reader.consume_float_optional("rpm_cutoff", 0.45f);
            comp.rpm_threshold = param_reader.consume_float_optional("rpm_threshold", 0.4f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<GS24<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Gyroscope") {
            Gyroscope<JitProvider> comp;
            
            comp.conductance = param_reader.consume_float_optional("conductance", 0.001f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Gyroscope<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "HighPowerLoad") {
            HighPowerLoad<JitProvider> comp;
            
            comp.power_draw = param_reader.consume_float_optional("power_draw", 500.0f);
            comp.min_voltage_diff = param_reader.consume_float_optional("min_voltage_diff", 0.01f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<HighPowerLoad<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "InertiaNode") {
            InertiaNode<JitProvider> comp;
            
            comp.mass = param_reader.consume_float_optional("mass", 1.0f);
            comp.damping = param_reader.consume_float_optional("damping", 0.5f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<InertiaNode<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Inverter") {
            Inverter<JitProvider> comp;
            
            comp.efficiency = param_reader.consume_float_optional("efficiency", 0.95f);
            comp.frequency = param_reader.consume_float_optional("frequency", 400.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Inverter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Radiator") {
            Radiator<JitProvider> comp;
            
            comp.cooling_capacity = param_reader.consume_float_optional("cooling_capacity", 1000.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Radiator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RU19A") {
            RU19A<JitProvider> comp;
            
            comp.target_rpm = param_reader.consume_float_optional("target_rpm", 16000.0f);
            comp.auto_start = param_reader.consume_bool_optional("auto_start", true);
            comp.spinup_inertia = param_reader.consume_float_optional("spinup_inertia", 1.0f);
            comp.spindown_inertia = param_reader.consume_float_optional("spindown_inertia", 0.02f);
            comp.crank_time = param_reader.consume_float_optional("crank_time", 2.0f);
            comp.ignition_time = param_reader.consume_float_optional("ignition_time", 3.0f);
            comp.start_timeout = param_reader.consume_float_optional("start_timeout", 30.0f);
            comp.t4_target = param_reader.consume_float_optional("t4_target", 400.0f);
            comp.t4_max = param_reader.consume_float_optional("t4_max", 750.0f);
            comp.ambient_temp = param_reader.consume_float_optional("ambient_temp", 20.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RU19A<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "RUG82") {
            RUG82<JitProvider> comp;
            
            comp.v_target = param_reader.consume_float_optional("v_target", 28.5f);
            comp.kp = param_reader.consume_float_optional("kp", 2.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<RUG82<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "SolenoidValve") {
            SolenoidValve<JitProvider> comp;
            
            comp.normally_closed = param_reader.consume_bool_optional("normally_closed", false);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<SolenoidValve<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Spring") {
            Spring<JitProvider> comp;
            
            comp.k = param_reader.consume_float_optional("k", 1000.0f);
            comp.c = param_reader.consume_float_optional("c", 10.0f);
            comp.rest_length = param_reader.consume_float_optional("rest_length", 0.1f);
            comp.compression_only = param_reader.consume_bool_optional("compression_only", false);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Spring<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "TempSensor") {
            TempSensor<JitProvider> comp;
            
            comp.sensitivity = param_reader.consume_float_optional("sensitivity", 1.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<TempSensor<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Transformer") {
            Transformer<JitProvider> comp;
            
            comp.ratio = param_reader.consume_float_optional("ratio", 1.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Transformer<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VoltageSense") {
            VoltageSense<JitProvider> comp;
            
            comp.gain = param_reader.consume_float_optional("gain", 1.0f);
            comp.offset = param_reader.consume_float_optional("offset", 0.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VoltageSense<JitProvider>>(result.devices[dev.name]));
        }
        // Phase 2 Slice 7: Controlled source / conductance components
        else if (dev.classname == "ControlledVoltageSource") {
            ControlledVoltageSource<JitProvider> comp;
            
            comp.gain = param_reader.consume_float_optional("gain", 1.0f);
            comp.offset = param_reader.consume_float_optional("offset", 0.0f);
            comp.min_v = param_reader.consume_float_optional("min_v", 0.0f);
            comp.max_v = param_reader.consume_float_optional("max_v", 30.0f);
            comp.r_internal = param_reader.consume_float_optional("r_internal", 0.1f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            // NOTE: ControlledVoltageSource is NOT scheduled for push electrical propagation.
            // It participates in the electrical solver as a TheveninSource with dynamic voltage.
        }
        else if (dev.classname == "ControlledCurrentSource") {
            ControlledCurrentSource<JitProvider> comp;
            
            comp.gain = param_reader.consume_float_optional("gain", 1.0f);
            comp.min_i = param_reader.consume_float_optional("min_i", 0.0f);
            comp.max_i = param_reader.consume_float_optional("max_i", 100.0f);
            comp.g_shunt = param_reader.consume_float_optional("g_shunt", 0.001f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledCurrentSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VariableConductance") {
            VariableConductance<JitProvider> comp;
            
            comp.g_min = param_reader.consume_float_optional("g_min", 0.001f);
            comp.g_max = param_reader.consume_float_optional("g_max", 10.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VariableConductance<JitProvider>>(result.devices[dev.name]));
        }
        else {
            throw std::runtime_error("Unknown component class '" + std::string(dev.classname) +
                "' for device '" + dev.name + "'. No factory handler registered.");
        }
    }

    // == Batch 7: Guardrail validation ==
    // Verify that solver-owned electrical propagators were NOT accidentally added
    // to the push scheduler. This would cause double-solve (push + solver) or
    // bypass the solver's conductance matrix solution.
    // Track device names that ended up in consumer_device_names and validate.
    for (const auto& name : consumer_device_names) {
        auto it_dev = std::find_if(devices.begin(), devices.end(),
            [&name](const DeviceInstance& d) { return d.name == name; });
        if (it_dev != devices.end()) {
            if (is_solver_owned_electrical_propagator(it_dev->classname)) {
                throw std::runtime_error(
                    std::string("Guardrail violation: solver-owned electrical propagator '") +
                    it_dev->classname + "' (device '" + it_dev->name +
                    "') was incorrectly added to push scheduler consumer list. "
                    "These components must only run via the electrical solver.");
            }
        }
    }

    // Deduplicate fixed_signals (RefNode may have been added above and in the loop above)
    std::sort(result.fixed_signals.begin(), result.fixed_signals.end());
    result.fixed_signals.erase(
        std::unique(result.fixed_signals.begin(), result.fixed_signals.end()),
        result.fixed_signals.end());

    // Sentinel is a fixed signal: it is always allocated at the end and never changes
    result.fixed_signals.push_back(result.signal_count - 1);

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

        for (const std::string& port_name : active_source_writer_ports_for(dev.classname)) {
            register_writer(dev.name, port_name);
        }
        // Note: RefNode is intentionally NOT registered as an active source
        // because it defines the reference (0V) rather than driving voltage
    }

    // Phase 3.2: Topological ordering of consumers (writer -> reader)
    // Sources already run before consumers. Here we order only consumer bucket.
    if (!consumer_device_names.empty()) {
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
            const auto output_ports = output_ports_for_class(dev.classname);
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

    // == Batch 2: Electrical Island Extraction ==
    // Extract electrical primitive elements from supported components and partition
    // into connected islands.
    //
    // Extraction strategy (Step 14/15):
    //   1. Metadata-driven: components with solver_role metadata are extracted
    //      generically from their role kind, port_map, and param_map.
    //   2. Classname fallback: wrapper components without metadata use hardcoded
    //      classname-based extraction (Battery, Generator, Resistor, IndicatorLight,
    //      CurrentSense). These remain intentionally transitional until wrappers
    //      are decomposed into primitives.
    //
    // Unsupported components are silently ignored.

    struct RawElement {
        ElectricalElementKind kind;
        uint32_t node_a;
        uint32_t node_b;
        float value_a;
        float value_b;
        size_t component_index;  // stable index for determinism
        std::string device_name;  // for handle assignment back to wrapper components
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
    // Does NOT use ParamReader — params were already validated in Phase 2.
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

    // Helper to read a solver_role param by role key, resolving to the actual param name
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
                    UINT32_MAX,  // unused
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
                    {}  // Metadata-driven primitives: no handle assignment needed
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
                    {}  // Metadata-driven primitives: no handle assignment needed
                });
            }
            continue;  // Metadata handled; skip classname fallback
        }

        // == Path 2: Classname-based fallback for wrapper components ==
        // These remain intentionally transitional until wrappers are decomposed
        // into primitives with solver_role metadata.
        if (dev.classname == "Battery") {
            // TheveninSource: value_a = source voltage, value_b = series resistance
            float v_nominal = read_param_float(dev, "v_nominal", 28.0f);
            float internal_r = read_param_float(dev, "internal_r", 0.01f);
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
        else if (dev.classname == "Generator") {
            // TheveninSource: value_a = source voltage, value_b = series resistance
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
            // ConductanceBranch: value_a = conductance
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
                {}  // Resistor does not need handle assignment
            });
        }
        else if (dev.classname == "IndicatorLight") {
            // ConductanceBranch: value_a = conductance
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
            // ConductanceBranch: value_a = conductance (high conductance = low series resistance)
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
        // -- Classname fallback for primitives without metadata --
        // When used via build_systems_dev() without library loading, solver_role
        // is not populated. These mirror the metadata-driven path but are
        // accessed only when solver_role is absent.
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
            // TheveninSource with dynamic voltage: initial value_a = clamp(0 * gain + offset, min_v, max_v)
            // Updated each frame before solve_electrical() from cmd signal (one-frame delay).
            float gain_val = read_param_float(dev, "gain", 1.0f);
            float offset_val = read_param_float(dev, "offset", 0.0f);
            float min_v_val = read_param_float(dev, "min_v", 0.0f);
            float max_v_val = read_param_float(dev, "max_v", 30.0f);
            float r_internal_val = read_param_float(dev, "r_internal", 0.1f);
            float initial_voltage = std::clamp(0.0f * gain_val + offset_val, min_v_val, max_v_val);
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
        // Unsupported components are silently ignored for electrical_plan
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
        std::map<uint32_t, std::vector<size_t>> island_members;  // root -> element indices
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
    // After islands are built, map each wrapper component (Battery, Generator,
    // IndicatorLight, CurrentSense) to its corresponding electrical primitive element.

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
                if constexpr (std::is_same_v<CompType, Battery<JitProvider>> ||
                              std::is_same_v<CompType, Generator<JitProvider>> ||
                              std::is_same_v<CompType, IndicatorLight<JitProvider>> ||
                              std::is_same_v<CompType, CurrentSense<JitProvider>> ||
                              std::is_same_v<CompType, ControlledVoltageSource<JitProvider>>) {
                    comp.electrical_handle = handle;
                }
            }, it->second);
        }
    }

    return result;
}
