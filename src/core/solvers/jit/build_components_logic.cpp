#include "jit_solver_internal.h"
#include "build_components_common.h"
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

namespace jit_solver_impl {

bool try_build_logic_component(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader)
{
    switch (dev.kind) {
    case ComponentKind::Add: {
        Add<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Subtract: {
        Subtract<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Multiply: {
        Multiply<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::Divide: {
        Divide<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::AND: {
        AND<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::OR: {
        OR<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::XOR: {
        XOR<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::NOT: {
        NOT<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    case ComponentKind::NAND: {
        NAND<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    default:
        return false;
    }
}

} // namespace jit_solver_impl