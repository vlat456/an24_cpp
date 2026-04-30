#pragma once

// =============================================================================
// AN-24 Binary Wire Protocol V2 — Delta + tier-based polling
// =============================================================================
//
// Zero-allocation, fixed-layout packed struct protocol for 60fps frame data
// exchange between SimConnect host and WASM bridge inside MSFS 2024.
//
// V2 protocol: instead of sending ALL values every frame, WASM maintains a
// shadow buffer and only sends changed records (delta update). Variables are
// partitioned into tiers by update frequency. Periodic full sync catches any
// missed deltas over the unreliable CommBus.
//
// Two CommBus channels:
//   "An24Bridge_Frame"   — binary (this protocol), every frame
//   "An24Bridge_Control" — JSON, setup/registration only
//
// Wire layout:
//   [PacketHeader(8B)] [VarRecord(8B)] × count
//
// Endianness: both wasm32 and x86_64 are little-endian — no byte-swapping.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

// =============================================================================
// CommBus channel names
// =============================================================================
namespace BridgeChannels {
    constexpr const char* Frame   = "An24Bridge_Frame";
    constexpr const char* Control = "An24Bridge_Control";
}

// =============================================================================
// Protocol constants
// =============================================================================

/// Packet magic bytes: 'A' 'N' = 0x414E (little-endian).
static constexpr uint16_t PACKET_MAGIC = 0x414E;

/// Protocol version 2 — delta protocol.
static constexpr uint8_t PROTOCOL_VERSION = 2;

/// Maximum practical packet size: header + 512 records.
static constexpr size_t MAX_PACKET_SIZE = 8 + 512 * 8;

// =============================================================================
// Tier constants — variable polling frequency
// =============================================================================
//
// Variables are partitioned into tiers by how often they change.
// The host sends a tier mask in DeltaRead to request specific tiers.
// The WASM bridge reads only the requested tier's variables from the Vars API.

static constexpr uint8_t TIER_FAST   = 0;  ///< Every frame (~80 vars: bus voltage, RPM, attitude)
static constexpr uint8_t TIER_MEDIUM = 1;  ///< Every 5th frame (~120 vars: switches, flaps, gear)
static constexpr uint8_t TIER_SLOW   = 2;  ///< Every 30th frame (~300 vars: temperature, pressure)

/// Tier bitmask constants for DeltaRead.count field.
static constexpr uint16_t TIER_MASK_FAST   = 0x01;  ///< bit0
static constexpr uint16_t TIER_MASK_MEDIUM = 0x02;  ///< bit1
static constexpr uint16_t TIER_MASK_SLOW   = 0x04;  ///< bit2
static constexpr uint16_t TIER_MASK_ALL    = 0x07;  ///< All tiers

/// Special tier mask value: force WASM to send FullSync instead of DeltaUpdate.
static constexpr uint16_t TIER_MASK_FORCE_FULL_SYNC = 0xFFFF;

/// Full sync interval: WASM sends all values every N frames as a safety net.
static constexpr uint16_t FULL_SYNC_INTERVAL = 60;

/// Maximum number of delta-tracked variables.
static constexpr uint16_t MAX_DELTA_VARS = 512;

// =============================================================================
// Cmd — packet type discriminator (V2)
// =============================================================================

/// V2 delta protocol packet commands.
enum class Cmd : uint8_t {
    // Delta hot-path (binary, every frame)
    DeltaRead   = 0x01,  ///< Host→WASM: "poll these tiers" (8-byte header, count=tier_mask)
    DeltaUpdate = 0x02,  ///< WASM→Host: only changed VarRecords since last response
    FullSync    = 0x03,  ///< WASM→Host: all values (periodic or forced)
    DeltaWrite  = 0x04,  ///< Host→WASM: only changed output VarRecords
    WriteAck    = 0x05,  ///< WASM→Host: write confirmation (8-byte header only)

    // Setup (JSON on control channel, same framing for diagnostics)
    RegisterNames     = 0x10,
    RegisterNamesResp = 0x11,

    // Diagnostics
    Ping = 0xF0,
    Pong = 0xF1,
};

// =============================================================================
// VarType — MSFS 2024 variable categories
// =============================================================================

/// Variable type discriminator — matches MSFS 2024 Vars API categories.
enum class VarType : uint8_t {
    AVar  = 0x01,  ///< Aircraft simulation variables (fsVarsAVarGet/Set)
    LVar  = 0x02,  ///< Local variables (fsVarsLVarGet/Set, FLOAT64)
    HEvent = 0x03, ///< H events (fsEventsHEventCall)
    BVar  = 0x04,  ///< Input event variables (fsVarsBVarGet/Set)
    EVar  = 0x05,  ///< Environment variables (fsVarsEVarGet/Set)
    IVar  = 0x06,  ///< Instrument variables (fsVarsIVarGet/Set)
    OVar  = 0x07,  ///< Component variables (fsVarsOVarGet/Set)
    ZVar  = 0x08,  ///< SimObject variables (fsVarsZVarGet/Set)
};

/// Convert VarType to human-readable string (for logging/debugging).
inline constexpr std::string_view var_type_name(VarType t) {
    switch (t) {
        case VarType::AVar:  return "AVar";
        case VarType::LVar:  return "LVar";
        case VarType::HEvent: return "HEvent";
        case VarType::BVar:  return "BVar";
        case VarType::EVar:  return "EVar";
        case VarType::IVar:  return "IVar";
        case VarType::OVar:  return "OVar";
        case VarType::ZVar:  return "ZVar";
        default:             return "Unknown";
    }
}

/// Parse a var_type string to enum. Returns true on success.
inline bool parse_var_type(std::string_view s, VarType& out) {
    if (s == "AVar")  { out = VarType::AVar;  return true; }
    if (s == "LVar")  { out = VarType::LVar;  return true; }
    if (s == "HEvent") { out = VarType::HEvent; return true; }
    if (s == "BVar")  { out = VarType::BVar;  return true; }
    if (s == "EVar")  { out = VarType::EVar;  return true; }
    if (s == "IVar")  { out = VarType::IVar;  return true; }
    if (s == "OVar")  { out = VarType::OVar;  return true; }
    if (s == "ZVar")  { out = VarType::ZVar;  return true; }
    return false;
}

// =============================================================================
// ValType — value representation
// =============================================================================

/// How to interpret the `value` field in VarRecord.
/// Most simulation values are Float32, but MSFS Vars API exposes
/// typed getters (FLOAT64, int32_t, bool). The wire format carries
/// the raw 4 bytes + a discriminator — the consumer interprets accordingly.
/// Note: FLOAT64 (LVar) is truncated to Float32 on the wire — acceptable
/// for aircraft systems simulation where float precision suffices.
enum class ValType : uint8_t {
    Float32 = 0x00,
    Int32   = 0x01,
    Bool    = 0x02,
};

// =============================================================================
// PacketHeader — 8 bytes, packed
// =============================================================================

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t magic    = PACKET_MAGIC;  ///< Must be 0x414E
    uint8_t  version  = PROTOCOL_VERSION;
    uint8_t  cmd      = 0;             ///< Cmd enum value
    uint16_t count    = 0;             ///< Number of VarRecord entries following this header
    uint16_t seq_id   = 0;             ///< Monotonically increasing ID for request/response correlation
};
static_assert(sizeof(PacketHeader) == 8, "PacketHeader must be exactly 8 bytes");
static_assert(alignof(PacketHeader) == 1, "PacketHeader must be packed (alignof=1)");

// =============================================================================
// VarRecord — 8 bytes, packed
// =============================================================================
//
// One record per variable. For read requests, `value` is ignored.
// For write requests and responses, `value` is interpreted per `val_type`.
//
// Layout:
//   [0]     var_type    (VarType)
//   [1]     val_type    (ValType)
//   [2-3]   name_id     (interned variable ID, FNV-1a hash)
//   [4-7]   value       (raw 4 bytes — use val_type to interpret)

union WireValue {
    float    f32;
    int32_t  i32;
    uint32_t u32;

    /// Default construct to zero — avoids uninitialized reads.
    constexpr WireValue() : u32(0) {}
    constexpr explicit WireValue(float v)    : f32(v) {}
    constexpr explicit WireValue(int32_t v)  : i32(v) {}
    constexpr explicit WireValue(uint32_t v) : u32(v) {}
    constexpr explicit WireValue(bool v)     : u32(v ? 1u : 0u) {}
};
static_assert(sizeof(WireValue) == 4, "WireValue must be exactly 4 bytes");

struct VarRecord {
    VarType  var_type = VarType::AVar;
    ValType  val_type = ValType::Float32;
    uint16_t name_id  = 0;      ///< Interned variable ID (FNV-1a hash)
    WireValue value   = {};     ///< Raw value — 4 bytes, interpret via val_type
};
static_assert(sizeof(VarRecord) == 8, "VarRecord must be exactly 8 bytes");
static_assert(alignof(VarRecord) == 1, "VarRecord must be packed (alignof=1)");

#pragma pack(pop)

// =============================================================================
// Inline helpers
// =============================================================================

/// Check if a raw byte buffer starts with a valid PacketHeader.
inline bool is_valid_header(const uint8_t* data, size_t len) {
    if (len < sizeof(PacketHeader)) return false;
    PacketHeader hdr;
    std::memcpy(&hdr, data, sizeof(PacketHeader));
    return hdr.magic == PACKET_MAGIC && hdr.version == PROTOCOL_VERSION;
}

/// Compute total packet size for a given record count.
inline constexpr size_t packet_size(uint16_t record_count) {
    return sizeof(PacketHeader) + static_cast<size_t>(record_count) * sizeof(VarRecord);
}

/// Get Cmd from header's cmd byte.
inline Cmd header_cmd(const PacketHeader& hdr) {
    return static_cast<Cmd>(hdr.cmd);
}

// =============================================================================
// Change detection — epsilon-based comparison for shadow buffer
// =============================================================================

/// Default epsilon per VarType category — controls change detection sensitivity.
/// Variables with smaller epsilon are detected more precisely.
constexpr float default_epsilon(VarType vt) {
    switch (vt) {
        case VarType::EVar:  return 0.5f;    // Environment: temperature, pressure — coarse
        case VarType::AVar:  return 0.01f;   // Aircraft: voltage, RPM — precise
        case VarType::LVar:  return 0.005f;  // Local: typically small increments
        case VarType::BVar:  return 0.005f;  // Input events: fine-grained
        case VarType::IVar:  return 0.01f;   // Instruments
        case VarType::OVar:  return 0.01f;   // Components
        case VarType::ZVar:  return 0.01f;   // SimObjects
        case VarType::HEvent: return 0.0f;   // HEvents: exact match (trigger semantics)
        default:             return 0.01f;
    }
}

/// Detect whether a value has changed enough to warrant transmission.
/// Float uses epsilon-based comparison with relative scaling.
/// Bool/Int use exact match. NaN detected via u32 (NaN ≠ NaN in float math).
inline bool value_changed(WireValue current, WireValue previous,
                          float epsilon, ValType vtype) {
    switch (vtype) {
        case ValType::Bool:
        case ValType::Int32:
            return current.u32 != previous.u32;

        case ValType::Float32: {
            // Exact match when epsilon is zero — bit-level comparison
            if (epsilon <= 0.0f) return current.u32 != previous.u32;

            // NaN transitions: epsilon comparison is unreliable (NaN - X = NaN,
            // NaN > threshold = false). Detect via bit-level comparison instead.
            if (std::isnan(current.f32) || std::isnan(previous.f32)) {
                return current.u32 != previous.u32;
            }

            float diff = std::abs(current.f32 - previous.f32);
            // Relative threshold: scale epsilon by magnitude of previous value,
            // floored at epsilon to avoid sub-epsilon noise on small values.
            float threshold = std::max(epsilon, std::abs(previous.f32) * epsilon);
            return diff > threshold;
        }
    }
    return current.u32 != previous.u32;
}

/// Default tier assignment when not specified in blueprint.
/// Fast-changing aircraft variables → Tier0, slow environment → Tier2.
constexpr uint8_t default_tier(VarType vt) {
    switch (vt) {
        case VarType::AVar:  return TIER_FAST;     // RPM, voltage — every frame
        case VarType::LVar:  return TIER_MEDIUM;   // Local vars — moderate
        case VarType::HEvent: return TIER_FAST;    // Events — immediate
        case VarType::BVar:  return TIER_MEDIUM;   // Input events — moderate
        case VarType::EVar:  return TIER_SLOW;     // Temperature, pressure — slow
        case VarType::IVar:  return TIER_MEDIUM;   // Instruments — moderate
        case VarType::OVar:  return TIER_SLOW;     // Components — slow
        case VarType::ZVar:  return TIER_SLOW;     // SimObjects — slow
        default:             return TIER_MEDIUM;
    }
}

/// Compute tier mask for a given frame counter.
/// Tier0 every frame, Tier1 every 5th, Tier2 every 30th.
inline uint16_t tier_mask_for_frame(uint32_t frame_counter) {
    uint16_t mask = TIER_MASK_FAST;
    if (frame_counter % 5 == 0)  mask |= TIER_MASK_MEDIUM;
    if (frame_counter % 30 == 0) mask |= TIER_MASK_SLOW;
    return mask;
}
