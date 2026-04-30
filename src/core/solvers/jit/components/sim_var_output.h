#pragma once

#include <string>

#include "core/solvers/common/provider.h"
#include "core/solvers/common/simvar_backend.h"

#include "../state.h"

/// SimVarOutput — writes a signal value to an MSFS 2024 simulation variable.
///
/// Two deployment modes:
///   - **JIT/Editor**: Component is a passthrough graph node. The SimConnectBridge
///     extracts output values from `values[]` after the scheduler runs and sends
///     them to MSFS. Component's `execute()` is a no-op — the bridge owns the I/O.
///   - **AOT/WASM**: Component resolves the variable at `pre_load()` via Vars API,
///     writes per-frame in `execute()` directly — no SimConnect/Bridge needed.
///
/// Supported variable types: AVar, LVar, HEvent, BVar, EVar, IVar, OVar, ZVar
///
/// Modes:
///   "data"  — Write value via fsVars*Set (default)
///   "event" — Trigger event via fsEventsHEventCall
template <typename Provider = JitProvider>
class SimVarOutput {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    std::string var_name;
    std::string var_type;   // "AVar", "LVar", "HEvent", "BVar", "EVar", "IVar", "OVar", "ZVar"
    std::string unit;       // MSFS unit (e.g. "Volts", "Celsius")
    int index = 0;          // 0-based index (blueprint convention)
    std::string mode;       // "data" (default) or "event"
    std::string event_name; // For event mode
    int event_id = 0;       // For event mode

    SimVarOutput() = default;

    /// Resolve variable name to handle at init time (AOT/WASM only).
    void pre_load() {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            if (!var_name.empty()) {
                handle_ = Backend::resolve(var_name, var_type, unit, index);
            }
        }
    }

    /// Per-frame: read input signal and write to variable.
    /// - AOT/WASM: writes directly to Vars API via resolved handle.
    /// - JIT/Editor: no-op — SimConnectBridge extracts values after step().
    void execute(SimulationState& st, double /*dt*/) {
        using Backend = typename SimVarBackendFor<Provider>::type;
        if constexpr (Backend::is_active) {
            if (handle_.valid) {
                float value = st.values[provider.get(PortNames::in)];
                Backend::write(handle_, value);
            }
        }
        // JIT: bridge.extract_outputs() reads from values[] after step() — do nothing here
    }

    void commit(SimulationState&, double) {}

private:
    SimVarHandle handle_{};
};
