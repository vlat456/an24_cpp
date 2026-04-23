#include "jit_solver_internal.h"
#include "build_components_common.h"

#include "components/switch.h"
#include "components/relay.h"
#include "components/hold_button.h"
#include "components/ref_node.h"
#include "components/generator.h"
#include "components/bus.h"
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
#include "components/accumulator.h"
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
#include "components/electric_heater.h"
#include "components/electric_pump.h"
#include "components/fuel_tank.h"
#include "components/gidro_accumulator.h"
#include "components/gyroscope.h"
#include "components/inertia_node.h"
#include "components/inverter.h"
#include "components/radiator.h"
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
#include <queue>
#include <spdlog/spdlog.h>

namespace jit_solver_impl {

void build_and_register_components(
    BuildResult& result,
    const std::vector<ResolvedDevice>& devices)
{
    std::vector<std::string> consumer_device_names;

    // Phase 2 Slice 1: Create and register migrated components
    for (const auto& dev : devices) {
        if (!has_component_metadata(dev.kind)) {
            throw std::runtime_error("Missing generated port metadata for component class '" + dev.classname + "'");
        }

        bool is_source = dev.scheduler_source;
        bool is_solver_owned = dev.solver_owned_electrical;

        // Guard: solver-owned electrical propagators must NOT be added to scheduler
        // as consumers. They are handled by the electrical solver instead.
        if (!is_source && !is_solver_owned) {
            consumer_device_names.push_back(dev.name);
        }

        ParamReader param_reader(dev.params, dev);

        auto build_knob_switch = [&](auto type_tag) {
            using CompType = decltype(type_tag);
            (void)type_tag;

            CompType comp;
            comp.positions = static_cast<int>(param_reader.consume_float_optional("positions", 2.0f));
            comp.positions = std::clamp(comp.positions, 2, KnobSwitch<JitProvider>::MAX_POSITIONS);
            comp.selected = static_cast<int>(param_reader.consume_float_optional("initial_position", 0.0f));
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();

            result.devices[dev.name] = comp;
        };

        // Handle each component type
        if (dev.classname == "Generator") {
            Generator<JitProvider> comp;
            comp.v_nominal = param_reader.consume_float_optional("v_nominal", 28.5f);
            comp.internal_r = param_reader.consume_float_optional("internal_r", 0.005f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "RefNode") {
            RefNode<JitProvider> comp;
            comp.value = param_reader.consume_float_optional("value", 0.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<RefNode<JitProvider>>(result.devices[dev.name]));
            const std::string key = dev.name + ".v";
            const ui::InternedId iid = result.signal_key_interner.lookup(key);
            auto it_sig = result.port_to_signal.find(iid);
            if (it_sig != result.port_to_signal.end()) {
                result.fixed_signals.push_back(it_sig->second);
            }
        }
        else if (dev.classname == "Value") {
            Value<JitProvider> comp;
            comp.value = param_reader.consume_float_optional("value", 0.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<Value<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Switch") {
            Switch<JitProvider> comp;
            comp.closed = param_reader.consume_bool_optional("closed", false);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Switch<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Relay") {
            Relay<JitProvider> comp;
            comp.closed = param_reader.consume_bool_optional("closed", false);
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (is_knob_switch_kind(dev.kind)) {
            if (dev.kind == ComponentKind::KnobSwitch) {
                build_knob_switch(KnobSwitch<JitProvider>{});
            }
            else if (dev.kind == ComponentKind::RotarySwitch1ToN) {
                build_knob_switch(RotarySwitch1ToN<JitProvider>{});
            }
            else {
                build_knob_switch(RotarySwitchNTo1<JitProvider>{});
            }
        }
        else if (dev.classname == "HoldButton") {
            HoldButton<JitProvider> comp;
            comp.idle = param_reader.consume_float_optional("idle", 0.0f);
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "Bus") {
            Bus<JitProvider> comp;
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Bus<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Comparator") {
            Comparator<JitProvider> comp;
            comp.Von = param_reader.consume_float_optional("Von", 5.0f);
            comp.Voff = param_reader.consume_float_optional("Voff", 2.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Comparator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "CurrentSense") {
            CurrentSense<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 1000.0f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<CurrentSense<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "AZS") {
            AZS<JitProvider> comp;
            comp.closed = param_reader.consume_bool_optional("closed", false);
            comp.i_nominal = param_reader.consume_float_optional("i_nominal", 20.0f);
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            comp.k_cool = param_reader.consume_float_optional("k_cool", 1.0f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "Resistor") {
            Resistor<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "ElectricalConductance") {
            ElectricalConductance<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "ElectricalSource") {
            ElectricalSource<JitProvider> comp;
            comp.voltage = param_reader.consume_float_optional("voltage", 28.0f);
            comp.resistance = param_reader.consume_float_optional("resistance", 0.01f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "Voltmeter") {
            Voltmeter<JitProvider> comp;
            comp.min = param_reader.consume_float_optional("min", 0.0f);
            comp.max = param_reader.consume_float_optional("max", 28.0f);
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Voltmeter<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "IndicatorLight") {
            IndicatorLight<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 1.0f);
            comp.rated_voltage = param_reader.consume_float_optional("rated_voltage", 28.0f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<IndicatorLight<JitProvider>>(result.devices[dev.name]));
        }
        else if (try_build_logic_component(result, dev, param_reader)) {
        }
        // Math/logical components
        else if (dev.classname == "Min") {
            Min<JitProvider> comp;
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Min<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Max") {
            Max<JitProvider> comp;
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Max<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Clamp") {
            Clamp<JitProvider> comp;
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Clamp<JitProvider>>(result.devices[dev.name]));
        }
        else if (try_build_control_component(result, dev, param_reader)) {
        }
        else if (try_build_utility_component(result, dev, param_reader)) {
        }
        else if (try_build_physical_component(result, dev, param_reader)) {
        }
        // Controlled source / conductance components
        else if (dev.classname == "ControlledVoltageSource") {
            ControlledVoltageSource<JitProvider> comp;
            comp.r_internal = param_reader.consume_float_optional("r_internal", 0.1f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "ControlledCurrentSource") {
            ControlledCurrentSource<JitProvider> comp;
            comp.gain = param_reader.consume_float_optional("gain", 1.0f);
            comp.min_i = param_reader.consume_float_optional("min_i", 0.0f);
            comp.max_i = param_reader.consume_float_optional("max_i", 100.0f);
            comp.g_shunt = param_reader.consume_float_optional("g_shunt", 0.001f);
            comp.pre_load();
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledCurrentSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VariableConductance") {
            VariableConductance<JitProvider> comp;
            setup_component_ports(result, dev, comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else {
            throw std::runtime_error("Unknown component class '" + std::string(dev.classname) +
                "' for device '" + dev.name + "'. No factory handler registered.");
        }
    }

    // Post-registration pass: Deduplicate and validate
    std::sort(result.fixed_signals.begin(), result.fixed_signals.end());
    result.fixed_signals.erase(
        std::unique(result.fixed_signals.begin(), result.fixed_signals.end()),
        result.fixed_signals.end());

    // Sentinel is a fixed signal: it is always allocated at the end and never changes
    result.fixed_signals.push_back(result.signal_count - 1);

    validate_source_writer_conflicts(result, devices);
    validate_consumer_guardrails(result, consumer_device_names, devices);
    topological_sort_consumers(result, consumer_device_names, devices);
}

}  // namespace
