#pragma once

#include "jit_solver_internal.h"
#include "../common/signal_key.h"

namespace jit_solver_impl {

/// Map resolved device ports to component provider signal indices.
template <typename Comp>
inline void setup_component_ports(
    BuildResult& result,
    const SolverDevice& dev,
    Comp& comp)
{
    for (const auto& [port_name, port] : dev.ports) {
        (void)port;
        const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
        const core::InternedId key = result.signal_key_interner.lookup(full_port);
        if (key.empty()) continue;
        auto it = result.port_to_signal.find(key);
        if (it != result.port_to_signal.end()) {
            auto port_enum = string_to_port_name(port_name);
            if (port_enum.has_value()) {
                comp.provider.set(port_enum.value(), it->second);
            }
        }
    }
}

} // namespace jit_solver_impl
