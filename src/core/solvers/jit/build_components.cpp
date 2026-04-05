#include "jit_solver_internal.h"

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
#include "components/high_power_load.h"
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
    const std::vector<DeviceInstance>& devices)
{
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
            setup_ports(comp);
            param_reader.validate_all_consumed();

            result.devices[dev.name] = comp;
        };

        // Handle each component type
        if (dev.classname == "Generator") {
            Generator<JitProvider> comp;
            comp.v_nominal = param_reader.consume_float_optional("v_nominal", 28.5f);
            comp.internal_r = param_reader.consume_float_optional("internal_r", 0.005f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "RefNode") {
            RefNode<JitProvider> comp;
            comp.value = param_reader.consume_float_optional("value", 0.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_source(&std::get<RefNode<JitProvider>>(result.devices[dev.name]));
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
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (is_knob_switch_family(dev.classname)) {
            if (dev.classname == "KnobSwitch") {
                build_knob_switch(KnobSwitch<JitProvider>{});
            }
            else if (dev.classname == "RotarySwitch1ToN") {
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
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
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
            param_reader.consume_string_optional("exposed_direction", "In");
            param_reader.consume_string_optional("exposed_type", "V");
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<BlueprintInput<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "BlueprintOutput") {
            BlueprintOutput<JitProvider> comp;
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
            comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
            comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
            comp.k_cool = param_reader.consume_float_optional("k_cool", 1.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "Resistor") {
            Resistor<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "ElectricalConductance") {
            ElectricalConductance<JitProvider> comp;
            comp.conductance = param_reader.consume_float_optional("conductance", 0.1f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
        }
        else if (dev.classname == "ElectricalSource") {
            ElectricalSource<JitProvider> comp;
            comp.voltage = param_reader.consume_float_optional("voltage", 28.0f);
            comp.resistance = param_reader.consume_float_optional("resistance", 0.01f);
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
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
            comp.conductance = param_reader.consume_float_optional("conductance", 1.0f);
            comp.rated_voltage = param_reader.consume_float_optional("rated_voltage", 28.0f);
            comp.pre_load();
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<IndicatorLight<JitProvider>>(result.devices[dev.name]));
        }
        // Math/logical components
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
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Clamp<JitProvider>>(result.devices[dev.name]));
        }
        // Control/filter components
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
        else if (dev.classname == "Accumulator") {
            Accumulator<JitProvider> comp;
            comp.initial_val = param_reader.consume_float_optional("initial_val", 0.0f);
            comp.state = comp.initial_val;
            comp.next_state = comp.initial_val;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Accumulator<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "Integrator") {
            Integrator<JitProvider> comp;
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
        // Utility logical components
        else if (dev.classname == "Normalize") {
            Normalize<JitProvider> comp;
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<Normalize<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "LUT") {
            LUT<JitProvider> comp;
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
        // Additional non-controlled components
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
            comp.initial_rpm = param_reader.consume_float_optional("initial_rpm", 1.0f);
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
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<VoltageSense<JitProvider>>(result.devices[dev.name]));
        }
        // Controlled source / conductance components
        else if (dev.classname == "ControlledVoltageSource") {
            ControlledVoltageSource<JitProvider> comp;
            comp.r_internal = param_reader.consume_float_optional("r_internal", 0.1f);
            comp.pre_load();
            setup_ports(comp);
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
            setup_ports(comp);
            param_reader.validate_all_consumed();
            result.devices[dev.name] = comp;
            result.scheduler.add_consumer(&std::get<ControlledCurrentSource<JitProvider>>(result.devices[dev.name]));
        }
        else if (dev.classname == "VariableConductance") {
            VariableConductance<JitProvider> comp;
            setup_ports(comp);
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

void validate_source_writer_conflicts(
    const BuildResult& result,
    const std::vector<DeviceInstance>& devices)
{
    // Build a map: signal_index -> list of (device.port) that are source_writers.
    // If any signal has more than one source_writer, it's a conflict.
    std::unordered_map<uint32_t, std::vector<std::string>> writers_by_signal;

    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        const auto sw_ports = active_source_writer_ports_for(dev.classname);
        for (const auto& port_name : sw_ports) {
            const std::string full_port = dev.name + "." + port_name;
            auto it = result.port_to_signal.find(full_port);
            if (it == result.port_to_signal.end()) {
                continue;
            }
            writers_by_signal[it->second].push_back(full_port);
        }
    }

    for (const auto& [signal_idx, writers] : writers_by_signal) {
        if (writers.size() > 1) {
            std::string detail;
            for (size_t i = 0; i < writers.size(); ++i) {
                if (i > 0) detail += ", ";
                detail += writers[i];
            }
            throw std::runtime_error(
                "Source conflict on signal " + std::to_string(signal_idx) +
                ": multiple source-writer ports drive the same wire: " + detail);
        }
    }
}

void validate_consumer_guardrails(
    const BuildResult& result,
    const std::vector<std::string>& consumer_device_names,
    const std::vector<DeviceInstance>& devices)
{
    (void)result;  // Currently unused but part of signature for future validation
    
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
}

void topological_sort_consumers(
    BuildResult& result,
    std::vector<std::string>& consumer_device_names,
    const std::vector<DeviceInstance>& devices)
{
    // Phase 3.2: Topological ordering of consumers (writer -> reader)
    if (consumer_device_names.empty()) {
        return;
    }

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

}  // namespace
