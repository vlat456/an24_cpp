#pragma once

#include "jit_solver_internal.h"
#include "../common/signal_key.h"

#include <type_traits>
#include <utility>

namespace jit_solver_impl {

template <typename Comp>
inline void setup_component_ports(
    BuildResult& result,
    const ResolvedDevice& dev,
    Comp& comp)
{
    for (const auto& [port_name, port] : dev.ports) {
        (void)port;
        const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
        auto it = result.port_to_signal.find(full_port);
        if (it != result.port_to_signal.end()) {
            auto port_enum = string_to_port_name(port_name);
            if (port_enum.has_value()) {
                comp.provider.set(port_enum.value(), it->second);
            }
        }
    }
}

template <typename Comp>
inline void register_component(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader,
    Comp&& comp)
{
    param_reader.validate_all_consumed();
    result.devices[dev.name] = std::forward<Comp>(comp);
}

template <typename Comp>
inline void register_component_consumer(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader,
    Comp&& comp)
{
    register_component(result, dev, param_reader, std::forward<Comp>(comp));
    result.scheduler.add_consumer(&std::get<std::decay_t<Comp>>(result.devices[dev.name]));
}

template <typename Comp>
inline void register_component_source(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader,
    Comp&& comp)
{
    register_component(result, dev, param_reader, std::forward<Comp>(comp));
    result.scheduler.add_source(&std::get<std::decay_t<Comp>>(result.devices[dev.name]));
}

} // namespace jit_solver_impl
