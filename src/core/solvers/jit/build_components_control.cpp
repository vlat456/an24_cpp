#include "jit_solver_internal.h"
#include "build_components_common.h"
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

namespace jit_solver_impl {

bool try_build_control_component(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader)
{
    switch (dev.kind) {
    case ComponentKind::PID: {
        PID<JitProvider> comp;
        comp.Kp = param_reader.consume_float_required("Kp");
        comp.Ki = param_reader.consume_float_required("Ki");
        comp.Kd = param_reader.consume_float_required("Kd");
        comp.output_min = param_reader.consume_float_required("output_min");
        comp.output_max = param_reader.consume_float_required("output_max");
        comp.filter_alpha = param_reader.consume_float_required("filter_alpha");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::PI: {
        PI<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::PD: {
        PD<JitProvider> comp;
        comp.Kp = param_reader.consume_float_required("Kp");
        comp.Kd = param_reader.consume_float_required("Kd");
        comp.filter_alpha = param_reader.consume_float_required("filter_alpha");
        comp.output_min = param_reader.consume_float_required("output_min");
        comp.output_max = param_reader.consume_float_required("output_max");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::P: {
        P<JitProvider> comp;
        comp.Kp = param_reader.consume_float_required("Kp");
        comp.output_min = param_reader.consume_float_required("output_min");
        comp.output_max = param_reader.consume_float_required("output_max");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Accumulator: {
        Accumulator<JitProvider> comp;
        comp.initial_val = param_reader.consume_float_optional("initial_val", 0.0f);
        comp.state = comp.initial_val;
        comp.next_state = comp.initial_val;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Integrator: {
        Integrator<JitProvider> comp;
        comp.initial_val = param_reader.consume_float_required("initial_val");
        comp.accumulator = comp.initial_val;
        comp.next_accumulator = comp.initial_val;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::SampleHold: {
        SampleHold<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::TimeDelay: {
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
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Monostable: {
        Monostable<JitProvider> comp;
        comp.duration = param_reader.consume_float_required("duration");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::SlewRate: {
        SlewRate<JitProvider> comp;
        comp.max_rate = param_reader.consume_float_required("max_rate");
        comp.deadzone = param_reader.consume_float_required("deadzone");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::AsymSlewRate: {
        AsymSlewRate<JitProvider> comp;
        comp.rate_up = param_reader.consume_float_required("rate_up");
        comp.rate_down = param_reader.consume_float_required("rate_down");
        comp.deadzone = param_reader.consume_float_required("deadzone");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::FastTMO: {
        FastTMO<JitProvider> comp;
        comp.tau = param_reader.consume_float_required("tau");
        comp.deadzone = param_reader.consume_float_optional("deadzone", 0.001f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::AsymTMO: {
        AsymTMO<JitProvider> comp;
        comp.tau_up = param_reader.consume_float_required("tau_up");
        comp.tau_down = param_reader.consume_float_required("tau_down");
        comp.deadzone = param_reader.consume_float_optional("deadzone", 0.001f);
        comp.pre_load();
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    default:
        return false;
    }
}

} // namespace jit_solver_impl