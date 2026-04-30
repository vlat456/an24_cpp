#pragma once

// =============================================================================
// SimVar WASM Backend — MSFS 2024 Vars API Implementation (Scaffold)
// =============================================================================
//
// This file is included in the WASM build via CMake include path.
// Currently a scaffold — real Vars API calls to be implemented.
//
// MSFS 2024 Vars API reference:
//   AVar:    fsVarsGetAVarId(), fsVarsAVarGet(), fsVarsAVarSet()
//   LVar:    fsVarsRegisterLVar(), fsVarsLVarGet(), fsVarsLVarSet()
//   HEvent:  fsEventsRegisterHEvent(), fsEventsHEventCall()
//   BVar:    fsVarsGetBVarId(), fsVarsBVarGet(), fsVarsBVarSet()
//   EVar:    fsVarsGetEVarId(), fsVarsEVarGet(), fsVarsEVarSet()
//   IVar:    fsVarsGetIVarId(), fsVarsIVarGet(), fsVarsIVarSet()
//   OVar:    fsVarsGetOVarId(), fsVarsOVarGet(), fsVarsOVarSet()
//   ZVar:    fsVarsGetZVarId(), fsVarsZVarGet(), fsVarsZVarSet()

/// AOT backend — active in WASM build.
/// Uses MSFS 2024 Vars API for direct variable access.
struct SimVarWasmBackend {
    static constexpr bool is_active = true;

    /// Resolve a variable name to a handle at init time (pre_load).
    /// var_type: "AVar", "LVar", "HEvent", "BVar", "EVar", "IVar", "OVar", "ZVar"
    /// unit: MSFS unit string (e.g. "Volts", "Celsius") — primarily for AVars
    /// index: 0-based index from blueprint (MSFS uses 1-based: "ENG RPM:1")
    static SimVarHandle resolve(std::string_view var_name,
                                std::string_view var_type,
                                std::string_view unit,
                                int index) {
        SimVarHandle handle;
        handle.index = index + 1;  // MSFS uses 1-based indexing

        if (var_type == "AVar") {
            handle.type = SimVarHandle::AVar;
            // TODO: handle.id = fsVarsGetAVarId(var_name.data());
            // TODO: handle.unit_id = fsVarsGetUnitId(unit.data());
        } else if (var_type == "LVar") {
            handle.type = SimVarHandle::LVar;
            // TODO: handle.id = fsVarsRegisterLVar(var_name.data());
        } else if (var_type == "HEvent") {
            handle.type = SimVarHandle::HEvent;
            // TODO: handle.id = fsEventsRegisterHEvent(var_name.data());
        } else if (var_type == "BVar") {
            handle.type = SimVarHandle::BVar;
            // TODO: handle.id = fsVarsGetBVarId(var_name.data());
        } else if (var_type == "EVar") {
            handle.type = SimVarHandle::EVar;
            // TODO: handle.id = fsVarsGetEVarId(var_name.data());
        } else if (var_type == "IVar") {
            handle.type = SimVarHandle::IVar;
            // TODO: handle.id = fsVarsGetIVarId(var_name.data());
        } else if (var_type == "OVar") {
            handle.type = SimVarHandle::OVar;
            // TODO: handle.id = fsVarsGetOVarId(var_name.data());
        } else if (var_type == "ZVar") {
            handle.type = SimVarHandle::ZVar;
            // TODO: handle.id = fsVarsGetZVarId(var_name.data());
        }

        // TODO: Set handle.valid = true when real API calls succeed
        (void)var_name;
        (void)unit;
        return handle;
    }

    /// Read a variable value. Returns 0.0f if handle is invalid.
    static float read(const SimVarHandle& handle) {
        if (!handle.valid) return 0.0f;

        double result = 0.0;
        switch (handle.type) {
            case SimVarHandle::AVar:
                // TODO: fsVarsAVarGet(handle.id, handle.unit_id, {}, &result, FS_OBJECT_ID_USER_AIRCRAFT);
                break;
            case SimVarHandle::LVar:
                // TODO: fsVarsLVarGet(handle.id, &result);
                break;
            case SimVarHandle::BVar:
                // TODO: fsVarsBVarGet(handle.id, &result);
                break;
            case SimVarHandle::EVar:
                // TODO: fsVarsEVarGet(handle.id, &result);
                break;
            case SimVarHandle::IVar:
                // TODO: fsVarsIVarGet(handle.id, &result);
                break;
            case SimVarHandle::OVar:
                // TODO: fsVarsOVarGet(handle.id, &result);
                break;
            case SimVarHandle::ZVar:
                // TODO: fsVarsZVarGet(handle.id, &result);
                break;
            case SimVarHandle::HEvent:
                // HEvents are not readable
                return 0.0f;
        }
        return static_cast<float>(result);
    }

    /// Write a variable value. No-op if handle is invalid.
    static void write(const SimVarHandle& handle, float value) {
        if (!handle.valid) return;

        double dval = static_cast<double>(value);
        switch (handle.type) {
            case SimVarHandle::AVar:
                // TODO: fsVarsAVarSet(handle.id, handle.unit_id, {}, dval, FS_OBJECT_ID_USER_AIRCRAFT);
                break;
            case SimVarHandle::LVar:
                // TODO: fsVarsLVarSet(handle.id, dval);
                break;
            case SimVarHandle::HEvent:
                // TODO: fsEventsHEventCall(handle.id, {});
                break;
            case SimVarHandle::BVar:
                // TODO: fsVarsBVarSet(handle.id, dval);
                break;
            case SimVarHandle::EVar:
                // TODO: fsVarsEVarSet(handle.id, dval);
                break;
            case SimVarHandle::IVar:
                // TODO: fsVarsIVarSet(handle.id, dval);
                break;
            case SimVarHandle::OVar:
                // TODO: fsVarsOVarSet(handle.id, dval);
                break;
            case SimVarHandle::ZVar:
                // TODO: fsVarsZVarSet(handle.id, dval);
                break;
        }
    }
};

/// Trait specialization: AotProvider → WASM backend (active in WASM build).
template <typename... Bindings>
struct SimVarBackendFor<AotProvider<Bindings...>> {
    using type = SimVarWasmBackend;
};
