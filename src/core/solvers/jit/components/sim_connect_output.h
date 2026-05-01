#pragma once

#include <string>

#include "core/solvers/common/provider.h"
#include "core/solvers/common/simvar_backend.h"

#include "../state.h"

/// SimConnectOutput — writes a signal value to an MSFS 2024 simulation variable via SimConnect.
///
/// JIT/Editor: passthrough node; SimConnectProvider extracts values after step.
/// AOT/WASM: resolves variable at pre_load(), writes per-frame via Vars API.
template <typename Provider = JitProvider>
class SimConnectOutput {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    std::string var_name;
    std::string var_type;
    std::string unit;
    std::string val_type;
    std::string mode;
    std::string event_name;
    int index = 0;
    int event_id = 0;

    SimConnectOutput() = default;

    void pre_load() {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            if (!var_name.empty()) {
                handle_ = Backend::resolve(var_name, var_type, unit, index);
            }
        }
    }

    void execute(SimulationState& st, double /*dt*/) {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            float value = st.values[provider.get(PortNames::in)];
            if (handle_.valid) {
                Backend::write(handle_, value);
            }
        }
    }

    void commit(SimulationState&, double) {}

private:
    SimVarHandle handle_{};
};
