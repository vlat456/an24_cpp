#include "jit_solver_internal.h"
#include "build_components_common.h"

namespace jit_solver_impl {

bool try_build_physical_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader)
{
    if (dev.classname == "ElectricHeater") {
        ElectricHeater<JitProvider> comp;
        comp.max_power = param_reader.consume_float_optional("max_power", 1000.0f);
        comp.efficiency = param_reader.consume_float_optional("efficiency", 0.9f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "ElectricPump") {
        ElectricPump<JitProvider> comp;
        comp.max_pressure = param_reader.consume_float_optional("max_pressure", 1000.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "FuelTank") {
        FuelTank<JitProvider> comp;
        comp.capacity = param_reader.consume_float_optional("capacity", 1000.0f);
        comp.level = param_reader.consume_float_optional("level", 1000.0f);
        comp.density = param_reader.consume_float_optional("density", 0.78f);
        comp.consumption_rate = param_reader.consume_float_optional("consumption_rate", 0.0f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "GidroAccumulator") {
        GidroAccumulator<JitProvider> comp;
        comp.precharge_pressure = param_reader.consume_float_optional("precharge_pressure", 50.0f);
        comp.volume = param_reader.consume_float_optional("volume", 10.0f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Gyroscope") {
        Gyroscope<JitProvider> comp;
        comp.conductance = param_reader.consume_float_optional("conductance", 0.001f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "HighPowerLoad") {
        HighPowerLoad<JitProvider> comp;
        comp.power_draw = param_reader.consume_float_optional("power_draw", 500.0f);
        comp.min_voltage_diff = param_reader.consume_float_optional("min_voltage_diff", 0.01f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "InertiaNode") {
        InertiaNode<JitProvider> comp;
        comp.initial_rpm = param_reader.consume_float_optional("initial_rpm", 1.0f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Inverter") {
        Inverter<JitProvider> comp;
        comp.efficiency = param_reader.consume_float_optional("efficiency", 0.95f);
        comp.frequency = param_reader.consume_float_optional("frequency", 400.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Radiator") {
        Radiator<JitProvider> comp;
        comp.cooling_capacity = param_reader.consume_float_optional("cooling_capacity", 1000.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "SolenoidValve") {
        SolenoidValve<JitProvider> comp;
        comp.normally_closed = param_reader.consume_bool_optional("normally_closed", false);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Spring") {
        Spring<JitProvider> comp;
        comp.k = param_reader.consume_float_optional("k", 1000.0f);
        comp.c = param_reader.consume_float_optional("c", 10.0f);
        comp.rest_length = param_reader.consume_float_optional("rest_length", 0.1f);
        comp.compression_only = param_reader.consume_bool_optional("compression_only", false);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "TempSensor") {
        TempSensor<JitProvider> comp;
        comp.sensitivity = param_reader.consume_float_optional("sensitivity", 1.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Transformer") {
        Transformer<JitProvider> comp;
        comp.ratio = param_reader.consume_float_optional("ratio", 1.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "VoltageSense") {
        VoltageSense<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }

    return false;
}

} // namespace jit_solver_impl
