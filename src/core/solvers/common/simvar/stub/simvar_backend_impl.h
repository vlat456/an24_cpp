#pragma once

// =============================================================================
// SimVar WASM Backend — Stub Implementation (Editor / macOS)
// =============================================================================
//
// This stub is used when compiling outside the MSFS WASM environment.
// All operations are no-ops. Components using if constexpr (Backend::is_active)
// will have all sim-var code eliminated by the compiler.
//
// The real WASM implementation lives in simvar/wasm/simvar_backend_impl.h
// and is selected via CMake include path for the WASM build.

/// AOT backend — inactive in stub build.
/// Real implementation uses MSFS 2024 Vars API:
///   resolve() → fsVarsGetAVarId() / fsVarsRegisterLVar() / fsEventsRegisterHEvent()
///   read()    → fsVarsAVarGet() / fsVarsLVarGet()
///   write()   → fsVarsAVarSet() / fsVarsLVarSet() / fsEventsHEventCall()
struct SimVarWasmBackend {
    static constexpr bool is_active = false;

    static SimVarHandle resolve(std::string_view /*var_name*/,
                                std::string_view /*var_type*/,
                                std::string_view /*unit*/,
                                int /*index*/) {
        return {};
    }

    static float read(const SimVarHandle& /*handle*/) { return 0.0f; }
    static void  write(const SimVarHandle& /*handle*/, float /*value*/) {}
};

/// Trait specialization: AotProvider → WASM backend.
/// In the stub build this is still inactive; in the real WASM build it becomes active.
template <typename... Bindings>
struct SimVarBackendFor<AotProvider<Bindings...>> {
    using type = SimVarWasmBackend;
};
