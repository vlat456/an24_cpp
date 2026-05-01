#pragma once

#include <string>

#include "core/solvers/common/provider.h"
#include "core/solvers/common/simvar_backend.h"

#include "../state.h"

/// SimConnectInput — reads an MSFS 2024 simulation variable via SimConnect.
///
/// JIT/Editor: passthrough node; SimConnectProvider injects values before step.
/// AOT/WASM: resolves variable at pre_load(), reads per-frame via Vars API.
template <typename Provider = JitProvider>
class SimConnectInput {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    std::string var_name;
    std::string var_type;
    std::string unit;
    std::string val_type;
    int index = 0;
    int tier = 1;
    float epsilon = 0.01f;
    float default_value = 0.0f;

    SimConnectInput() = default;

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
            float value = default_value;
            if (handle_.valid) {
                value = Backend::read(handle_);
            }
            st.values[provider.get(PortNames::out)] = value;
        }
    }

    void commit(SimulationState&, double) {}

private:
    SimVarHandle handle_{};
};
