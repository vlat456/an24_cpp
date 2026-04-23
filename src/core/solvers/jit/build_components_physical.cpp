#include "jit_solver_internal.h"
#include "build_components_common.h"
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

namespace jit_solver_impl {

bool try_build_physical_component(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader)
{
    switch (dev.kind) {
    case ComponentKind::ElectricHeater: {
        ElectricHeater<JitProvider> comp;
        comp.max_power = param_reader.consume_float_optional("max_power", 1000.0f);
        comp.efficiency = param_reader.consume_float_optional("efficiency", 0.9f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::ElectricPump: {
        ElectricPump<JitProvider> comp;
        comp.max_pressure = param_reader.consume_float_optional("max_pressure", 1000.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::FuelTank: {
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
    case ComponentKind::GidroAccumulator: {
        GidroAccumulator<JitProvider> comp;
        comp.precharge_pressure = param_reader.consume_float_optional("precharge_pressure", 50.0f);
        comp.volume = param_reader.consume_float_optional("volume", 10.0f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Gyroscope: {
        Gyroscope<JitProvider> comp;
        comp.conductance = param_reader.consume_float_optional("conductance", 0.001f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::InertiaNode: {
        InertiaNode<JitProvider> comp;
        comp.initial_rpm = param_reader.consume_float_optional("initial_rpm", 1.0f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Inverter: {
        Inverter<JitProvider> comp;
        comp.efficiency = param_reader.consume_float_optional("efficiency", 0.95f);
        comp.frequency = param_reader.consume_float_optional("frequency", 400.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Radiator: {
        Radiator<JitProvider> comp;
        comp.cooling_capacity = param_reader.consume_float_optional("cooling_capacity", 1000.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::SolenoidValve: {
        SolenoidValve<JitProvider> comp;
        comp.normally_closed = param_reader.consume_bool_optional("normally_closed", false);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Spring: {
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
    case ComponentKind::TempSensor: {
        TempSensor<JitProvider> comp;
        comp.sensitivity = param_reader.consume_float_optional("sensitivity", 1.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Transformer: {
        Transformer<JitProvider> comp;
        comp.ratio = param_reader.consume_float_optional("ratio", 1.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::VoltageSense: {
        VoltageSense<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    default:
        return false;
    }
}

} // namespace jit_solver_impl