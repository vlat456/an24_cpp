#pragma once

#include <string>

#include "core/solvers/common/provider.h"
#include "core/solvers/common/simvar_backend.h"

#include "../state.h"

/// SimVarInput — reads an MSFS 2024 simulation variable and outputs it as a signal.
///
/// Two deployment modes:
///   - **JIT/Editor**: Component is a passthrough graph node. The SimConnectBridge
///     injects real MSFS values via `apply_typed_overrides()` before the scheduler
///     runs. Component's `execute()` is a no-op — the bridge owns the I/O.
///   - **AOT/WASM**: Component resolves the variable at `pre_load()` via Vars API,
///     reads per-frame in `execute()` directly — no SimConnect/Bridge needed.
///
/// Supported variable types: AVar, LVar, HEvent, BVar, EVar, IVar, OVar, ZVar
template <typename Provider = JitProvider>
class SimVarInput {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    std::string var_name;
    std::string var_type;   // "AVar", "LVar", "HEvent", "BVar", "EVar", "IVar", "OVar", "ZVar"
    std::string unit;       // MSFS unit (e.g. "Volts", "Celsius")
    int index = 0;          // 0-based index (blueprint convention)
    float default_value = 0.0f;

    SimVarInput() = default;

    /// Resolve variable name to handle at init time (AOT/WASM only).
    void pre_load() {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            if (!var_name.empty()) {
                handle_ = Backend::resolve(var_name, var_type, unit, index);
            }
        }
    }

    /// Per-frame: read variable value and write to output signal.
    /// - AOT/WASM: reads directly from Vars API via resolved handle.
    /// - JIT/Editor: no-op — SimConnectBridge injects values via apply_typed_overrides().
    void execute(SimulationState& st, double /*dt*/) {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            float value = default_value;
            if (handle_.valid) {
                value = Backend::read(handle_);
            }
            st.values[provider.get(PortNames::out)] = value;
        }
        // JIT: bridge.inject_inputs() already wrote to values[] — do not overwrite
    }

    void commit(SimulationState&, double) {}

private:
    SimVarHandle handle_{};
};
