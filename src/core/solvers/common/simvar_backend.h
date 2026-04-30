#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

#include "core/solvers/common/provider.h"

// =============================================================================
// SimVar Backend Trait System
// =============================================================================
//
// Selects between Bridge (JIT/Editor) and WASM (AOT/MSFS) sim-variable backends
// at compile time via CMake include paths. Zero #ifdef in source files.
//
// Usage in components:
//   using Backend = typename SimVarBackendFor<Provider>::type;
//   if constexpr (Backend::is_active) { ... }
//
// The backend implementation header is resolved by CMake include path:
//   - Editor build:  src/core/solvers/common/simvar/stub/
//   - WASM build:    src/core/solvers/common/simvar/wasm/
// Both directories provide simvar_backend_impl.h with SimVarWasmBackend defined.

/// Opaque handle to a resolved MSFS variable.
/// Populated during pre_load() by Backend::resolve().
/// Execute() no-ops safely when handle is invalid.
struct SimVarHandle {
    uint32_t id = std::numeric_limits<uint32_t>::max();
    /// Variable type discriminator — matches MSFS 2024 Vars API categories.
    /// Wire protocol uses the same values via VarType enum in wire_protocol.h.
    enum Type : uint8_t {
        AVar  = 0,   ///< Aircraft simulation variables (fsVarsAVarGet/Set)
        LVar  = 1,   ///< Local variables (fsVarsLVarGet/Set, FLOAT64 only)
        HEvent = 2,  ///< H events (fsEventsHEventCall)
        BVar  = 3,   ///< Input event variables (fsVarsBVarGet/Set)
        EVar  = 4,   ///< Environment variables (fsVarsEVarGet/Set)
        IVar  = 5,   ///< Instrument variables (fsVarsIVarGet/Set)
        OVar  = 6,   ///< Component variables (fsVarsOVarGet/Set)
        ZVar  = 7,   ///< SimObject variables (fsVarsZVarGet/Set)
    } type = AVar;
    int unit_id = 0;   // For AVars: MSFS unit ID (0 = none)
    int index = 0;     // For indexed variables (0-based in blueprint, +1 for MSFS)
    bool valid = false;
};

/// JIT backend — always inactive.
/// All methods are no-ops. if constexpr eliminates dead code in JIT components.
struct SimVarBridgeBackend {
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

/// Trait: maps a Provider type to its corresponding SimVar backend.
/// Specializations are defined in simvar_backend_impl.h (resolved by CMake include path).
template <typename Provider>
struct SimVarBackendFor;

// Default: JitProvider maps to bridge backend (inactive)
template <>
struct SimVarBackendFor<JitProvider> {
    using type = SimVarBridgeBackend;
};

// AotProvider specialization — defined in simvar_backend_impl.h (resolved by CMake include path)

// Include the backend implementation (resolved by CMake include path)
#include "simvar_backend_impl.h"
