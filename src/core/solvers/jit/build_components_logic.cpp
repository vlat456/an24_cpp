#include "jit_solver_internal.h"
#include "build_components_common.h"

namespace jit_solver_impl {

bool try_build_logic_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader)
{
    if (dev.classname == "Add") {
        Add<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Subtract") {
        Subtract<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Multiply") {
        Multiply<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Divide") {
        Divide<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }

    if (dev.classname == "AND") {
        AND<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "OR") {
        OR<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "XOR") {
        XOR<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "NOT") {
        NOT<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "NAND") {
        NAND<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }

    return false;
}

} // namespace jit_solver_impl
