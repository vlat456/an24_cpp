#pragma once

#include "wire_protocol.h"
#include "core/strings/interned_id.h"

#include <cstdint>
#include <string>

/// Direction of data flow for a sim variable mapping.
enum class SimVarDirection : uint8_t {
    Input,   // MSFS → simulator (SimConnectInput)
    Output,  // simulator → MSFS (SimConnectOutput)
};

/// Write mode for output variables.
enum class SimVarMode : uint8_t {
    Data,   // Set variable value (fsVarsAVarSet / fsVarsLVarSet)
    Event,  // Trigger event (fsEventsHEventCall)
};

/// Mapping between a simulator signal and an MSFS variable.
/// Populated by build_mappings() from SimConnectInput/SimConnectOutput node params.
struct SimVarMapping {
    std::string var_name;
    VarType     var_type = VarType::AVar;  ///< Parsed from string param
    std::string unit;
    int         index = 0;
    SimVarDirection direction = SimVarDirection::Input;
    SimVarMode  mode = SimVarMode::Data;
    float       default_value = 0.0f;
    uint32_t    signal_index = UINT32_MAX;  ///< Index into SimulationState::values[]
    core::InternedId signal_key;            ///< Interned key for apply_typed_overrides()
    uint16_t    intern_id = 0;              ///< FNV-1a hash ID for binary wire protocol

    // V2 delta protocol fields
    uint8_t     tier = TIER_MEDIUM;         ///< Polling tier: TIER_FAST/MEDIUM/SLOW
    float       epsilon = 0.01f;           ///< Change detection threshold
    ValType     val_type = ValType::Float32; ///< Wire value type for this signal
};
