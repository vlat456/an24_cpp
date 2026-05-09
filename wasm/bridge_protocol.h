// =============================================================================
// AN-24 WASM Bridge Protocol — WASM-side protocol helpers
// =============================================================================
//
// WASM-side protocol utilities for the V2 delta protocol.
// Provides:
//   1. DeltaEntry management (shadow buffer add/update/reset)
//   2. Variable name registration via Vars API
//   3. Epoch/seq_id helpers
//   4. Tier filter predicate
//
// This file is the WASM-side counterpart to WireCodec (host side).
// WireCodec handles packet encoding/decoding; this handles the
// WASM-side shadow-buffer lifecycle and Vars API dispatch.
//
// Reference in panel.cfg:
//   htmlgauge00=WasmInstrument/WasmInstrument.html?wasm_module=an24_bridge.wasm&wasm_gauge=an24_bridge, 0,0,1,1

#pragma once

#include "simconnect/wire_protocol.h"

#include <cstdint>
#include <cstring>

// =============================================================================
// Shadow Buffer — delta tracking per variable
// =============================================================================

/// Per-variable shadow buffer entry.
/// Tracks the last-sent value for change detection.
struct BridgeDeltaEntry {
    WireValue last_sent;     ///< Last value sent to host
    float     epsilon;       ///< Change detection threshold
    uint8_t   tier;          ///< TIER_FAST / TIER_MEDIUM / TIER_SLOW
    VarType   var_type;      ///< AVar, LVar, BVar, etc.
    ValType   val_type;      ///< Float32, Int32, Bool
    uint16_t  name_id;       ///< Interned name ID (FNV-1a)
    int       vars_id;       ///< Resolved Vars API ID (fsVarsGetAVarId result)
    bool      valid;         ///< Has been populated at least once
};

/// The WASM-side shadow buffer — tracks all registered variables.
/// Shared between the bridge protocol layer and the CommBus handlers.
extern BridgeDeltaEntry g_delta_entries[];
extern uint16_t        g_delta_count;

// =============================================================================
// Delta Entry Helpers
// =============================================================================

/// Add a new variable to the shadow buffer.
/// Returns the index, or UINT16_MAX if full.
inline uint16_t bridge_add_var(
    VarType           var_type,
    ValType           val_type,
    uint8_t           tier,
    float             epsilon,
    uint16_t          name_id,
    int               vars_api_id)
{
    if (g_delta_count >= MAX_DELTA_VARS)
        return UINT16_MAX;

    uint16_t idx = g_delta_count++;
    auto& entry = g_delta_entries[idx];
    entry.last_sent = WireValue(0.0f);
    entry.epsilon   = epsilon;
    entry.tier      = tier;
    entry.var_type  = var_type;
    entry.val_type  = val_type;
    entry.name_id   = name_id;
    entry.vars_id   = vars_api_id;
    entry.valid     = false;
    return idx;
}

/// Reset the shadow buffer (e.g. on module reinit).
inline void bridge_reset_vars() {
    g_delta_count = 0;
}

/// Check if a bridge entry is in the requested tier(s).
inline bool bridge_tier_match(const BridgeDeltaEntry& entry, uint16_t tier_mask) {
    uint8_t tier_bit = static_cast<uint8_t>(1u << entry.tier);
    return (tier_mask & tier_bit) != 0;
}

// =============================================================================
// Variable Registration — Build JSON registration payload
// =============================================================================

/// Maximum number of variable name bytes in a single registration message.
static constexpr size_t REG_BUF_SIZE = 4096;

/// Build a JSON "register_names" command for the control channel.
/// The host uses this to learn which variables are available and their
/// tier/epsilon configuration.
///
/// Returns bytes written, or 0 if the buffer was too small.
size_t bridge_build_registration_json(char* buf, size_t buf_size);
