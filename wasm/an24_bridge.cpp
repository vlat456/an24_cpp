#include "an24_bridge.h"
#include "bridge_protocol.h"
#include "simconnect/wire_codec.h"
#include "simconnect/wire_protocol.h"

#include <cstdint>
#include <cstring>
#include <cmath>

// MSFS 2024 WASM SDK headers — only available in WASM build.
// Host-side compilation (macOS editor) includes wire_protocol/wire_codec only.
#if __has_include(<MSFS/MSFS.h>)
    #include <MSFS/MSFS.h>
    #include <MSFS/MSFS_Vars.h>
    #include <MSFS/MSFS_CommBus.h>
    #include <MSFS/MSFS_Event.h>
    #include <MSFS/Legacy/gauges.h>
    #define HAS_MSFS_SDK 1
#else
    #define HAS_MSFS_SDK 0
#endif

// =============================================================================
// Fallback type definitions for host-side compilation (no MSFS SDK)
// =============================================================================
#if !HAS_MSFS_SDK
    #define MSFS_CALLBACK
    using FsContext = void*;
    struct sGaugeInstallData { int iSizeX; int iSizeY; };
    struct sGaugeDrawData    { int fbWidth; int fbHeight; int winWidth; int winHeight; };
    struct sSystemInstallData { const char* parameterString; };
    enum class FsCallFlags : uint32_t { FsCall_Wasm = 0 };
    using fsCommBusWasmCallback = void (*)(const char*, unsigned int, void*);
    // NOLINTBEGIN(bugprone-macro-parentheses)
    inline void fsCommBusRegister(const char*, fsCommBusWasmCallback, void*) {}
    inline void fsCommBusUnregisterAll() {}
    // NOLINTEND(bugprone-macro-parentheses)
#endif

// =============================================================================
// Module State
// =============================================================================

uint32_t g_module_frame_count = 0;
bool     g_module_initialized = false;

// Shadow buffer — shared with bridge_protocol.h
BridgeDeltaEntry g_delta_entries[MAX_DELTA_VARS];
uint16_t         g_delta_count = 0;

// Buffers (static, zero heap allocation on hot path)
static uint8_t   send_buffer[MAX_PACKET_SIZE];
static VarRecord response_records[MAX_DELTA_VARS];
static uint16_t  frames_since_sync = 0;
static uint16_t  last_host_epoch = 0;

// =============================================================================
// Vars API Helpers — dispatch by VarType
// =============================================================================

/// Resolve a Vars API ID from a variable name and type.
/// Called during registration (control channel JSON processing).
/// Returns -1 if the variable cannot be resolved.
static int resolve_var_id(const char* name, VarType type) {
#if HAS_MSFS_SDK
    switch (type) {
        case VarType::AVar:   return fsVarsGetAVarId(name);
        case VarType::LVar:   return fsVarsGetLVarId(name);
        case VarType::BVar:   return fsVarsGetBVarId(name);
        case VarType::EVar:   return fsVarsGetEnvironmentVarId(name);
        case VarType::OVar:   return fsVarsGetOVarId(name);
        case VarType::ZVar:   return fsVarsGetZVarId(name);
        default:              return -1;
    }
#else
    (void)name; (void)type;
    return -1;
#endif
}

/// Read a single variable from the Vars API by resolved ID.
/// Returns the current value, or 0 if the ID is invalid (-1).
static double read_var(int vars_id, VarType type) {
    if (vars_id < 0) return 0.0;
#if HAS_MSFS_SDK
    switch (type) {
        case VarType::AVar:   return fsVarsAVarGet(vars_id);
        case VarType::LVar:   return fsVarsLVarGet(vars_id);
        case VarType::BVar:   return fsVarsBVarGet(vars_id);
        case VarType::EVar:   return fsVarsEnvironmentVarGet(vars_id);
        case VarType::IVar:   return fsVarsIVarGet(vars_id);
        case VarType::OVar:   return fsVarsOVarGet(vars_id);
        case VarType::ZVar:   return fsVarsZVarGet(vars_id);
        default:              return 0.0;
    }
#else
    (void)vars_id; (void)type;
    return 0.0;
#endif
}

/// Write a single variable to the Vars API by resolved ID.
static void write_var(int vars_id, VarType type, double value) {
    if (vars_id < 0) return;
#if HAS_MSFS_SDK
    switch (type) {
        case VarType::AVar:   fsVarsAVarSet(vars_id, value); break;
        case VarType::LVar:   fsVarsLVarSet(vars_id, value); break;
        case VarType::BVar:   fsVarsBVarSet(vars_id, value); break;
        case VarType::IVar:   fsVarsIVarSet(vars_id, value); break;
        case VarType::OVar:   fsVarsOVarSet(vars_id, value); break;
        case VarType::ZVar:   fsVarsZVarSet(vars_id, value); break;
        default:              break;
    }
#else
    (void)vars_id; (void)type; (void)value;
#endif
}

// =============================================================================
// CommBus Transport
// =============================================================================

static void send_response(size_t packet_size) {
    if (packet_size == 0) return;
#if HAS_MSFS_SDK
    fsCommBusCall(BridgeChannels::Frame,
                  reinterpret_cast<const char*>(send_buffer),
                  static_cast<unsigned int>(packet_size),
                  FsCallFlags::FsCall_Wasm);
#else
    (void)packet_size;
#endif
}

// =============================================================================
// DeltaRead / FullSync / DeltaUpdate Handlers
// =============================================================================

static void handle_full_sync(const PacketHeader& hdr) {
    frames_since_sync = 0;
    uint16_t count = (g_delta_count > MAX_DELTA_VARS) ? MAX_DELTA_VARS : g_delta_count;

    for (uint16_t i = 0; i < count; ++i) {
        auto& entry = g_delta_entries[i];
        response_records[i].var_type = entry.var_type;
        response_records[i].val_type = entry.val_type;
        response_records[i].name_id  = entry.name_id;

        double raw = read_var(entry.vars_id, entry.var_type);
        response_records[i].value = WireValue(static_cast<float>(raw));
        entry.last_sent = response_records[i].value;
        entry.valid = true;
    }

    send_response(WireCodec::build_full_sync(
        send_buffer, MAX_PACKET_SIZE,
        {response_records, count}, hdr.seq_id));
}

static void handle_delta_update(const PacketHeader& hdr, uint16_t tier_mask) {
    uint16_t changed_count = 0;

    for (uint16_t i = 0; i < g_delta_count; ++i) {
        auto& entry = g_delta_entries[i];
        if (!bridge_tier_match(entry, tier_mask))
            continue;

        double raw = read_var(entry.vars_id, entry.var_type);
        WireValue current(static_cast<float>(raw));

        if (entry.valid &&
            !value_changed(current, entry.last_sent,
                           entry.epsilon, entry.val_type)) {
            continue;
        }

        response_records[changed_count].var_type = entry.var_type;
        response_records[changed_count].val_type = entry.val_type;
        response_records[changed_count].name_id  = entry.name_id;
        response_records[changed_count].value    = current;
        entry.last_sent = current;
        entry.valid = true;
        changed_count++;
    }

    if (changed_count > 0) {
        send_response(WireCodec::build_delta_update(
            send_buffer, MAX_PACKET_SIZE,
            {response_records, changed_count}, hdr.seq_id));
    }
}

static void handle_delta_read(const PacketHeader& hdr) {
    uint16_t tier_mask = hdr.count;
    bool force_full = (tier_mask == TIER_MASK_FORCE_FULL_SYNC);

    if (hdr.seq_id > 0 && last_host_epoch > 0) {
        uint16_t gap = hdr.seq_id - last_host_epoch;
        if (gap > 1) force_full = true;
    }
    last_host_epoch = hdr.seq_id;
    frames_since_sync++;

    if (force_full || frames_since_sync >= FULL_SYNC_INTERVAL)
        handle_full_sync(hdr);
    else
        handle_delta_update(hdr, tier_mask);
}

// =============================================================================
// DeltaWrite Handler
// =============================================================================

static void handle_delta_write(const WireCodec::ParseResult& result,
                                const PacketHeader& hdr) {
    for (const auto& rec : result.records) {
        // Look up by name_id in delta table
        for (uint16_t i = 0; i < g_delta_count; ++i) {
            if (g_delta_entries[i].name_id == rec.name_id &&
                g_delta_entries[i].var_type == rec.var_type) {

                float val = (rec.val_type == ValType::Float32) ? rec.value.f32
                          : (rec.val_type == ValType::Int32)   ? static_cast<float>(rec.value.i32)
                          : (rec.val_type == ValType::Bool)    ? (rec.value.u32 ? 1.0f : 0.0f)
                          : 0.0f;
                write_var(g_delta_entries[i].vars_id,
                          g_delta_entries[i].var_type,
                          static_cast<double>(val));
                break;
            }
        }
    }

    send_response(WireCodec::build_write_ack(
        send_buffer, MAX_PACKET_SIZE, hdr.seq_id));
}

// =============================================================================
// Ping / Pong
// =============================================================================

static void handle_ping(const PacketHeader& hdr) {
    send_response(WireCodec::build_pong(
        send_buffer, MAX_PACKET_SIZE, hdr.seq_id));
}

// =============================================================================
// CommBus Frame Dispatcher
// =============================================================================

static void on_frame_request(const char* payload, unsigned int size, void* /*ctx*/) {
    if (size < sizeof(PacketHeader)) return;

    const auto* data = reinterpret_cast<const uint8_t*>(payload);
    auto result = WireCodec::parse(data, size);
    if (!result.header) return;

    PacketHeader hdr = *result.header;
    switch (header_cmd(hdr)) {
        case Cmd::DeltaRead:  handle_delta_read(hdr); break;
        case Cmd::DeltaWrite: handle_delta_write(result, hdr); break;
        case Cmd::Ping:       handle_ping(hdr); break;
        default: break;
    }
}

// =============================================================================
// CommBus Control Handler — Variable Registration via JSON
// =============================================================================
//
// Expected JSON format:
//   {"cmd":"register_names","vars":[
//     {"name":"SIM VAR NAME","type":"AVar","tier":0,"epsilon":0.01},
//     ...
//   ]}
//
// For each entry:
//   1. Resolve Vars API ID via resolve_var_id()
//   2. Compute interned name_id via compute_intern_id()
//   3. Store in g_delta_entries[] with tier/epsilon configuration

static void on_control_request(const char* payload, unsigned int size, void* /*ctx*/) {
    if (size == 0 || !payload) return;

    // Parse JSON registration (nlohmann/json not available in WASM build —
    // use a minimal inline parser for the known registration format).
    //
    // The registration message is small and predictable — we scan for
    // "name":, "type":, "tier":, "epsilon": tokens without a full JSON lib.
    //
    // Format: {"cmd":"register_names","vars":[{"name":"...","type":"...","tier":N,"epsilon":F},...]}

    const char* p = payload;
    const char* end = payload + size;

    // Scan for "vars":[...] array
    while (p < end) {
        // Look for '"name":"'
        if (*p == '"' && end - p > 7 && p[1] == 'n' && p[2] == 'a' &&
            p[3] == 'm' && p[4] == 'e' && p[5] == '"' && p[6] == ':') {
            p += 7;
            if (*p != '"') continue;
            p++; // skip opening quote

            // Extract name
            const char* name_start = p;
            while (p < end && *p != '"') p++;
            if (p >= end) break;
            size_t name_len = static_cast<size_t>(p - name_start);
            p++; // skip closing quote

            // Defaults
            VarType var_type = VarType::AVar;
            ValType val_type = ValType::Float32;
            uint8_t tier = 1;
            float epsilon = 0.01f;

            // Scan remaining fields in this object
            while (p < end && *p != '}') {
                if (*p == '"' && end - p > 5 && p[1] == 't' && p[2] == 'y' &&
                    p[3] == 'p' && p[4] == 'e' && p[5] == '"') {
                    p += 7; // skip "type":
                    if (*p == '"') {
                        p++;
                        if (end - p >= 4 && p[0] == 'A' && p[1] == 'V' && p[2] == 'a' && p[3] == 'r') {
                            var_type = VarType::AVar; val_type = ValType::Float32;
                        } else if (end - p >= 4 && p[0] == 'L' && p[1] == 'V' && p[2] == 'a' && p[3] == 'r') {
                            var_type = VarType::LVar; val_type = ValType::Float32;
                        } else if (end - p >= 4 && p[0] == 'B' && p[1] == 'V' && p[2] == 'a' && p[3] == 'r') {
                            var_type = VarType::BVar; val_type = ValType::Bool;
                        }
                        while (p < end && *p != '"' && *p != ',' && *p != '}') p++;
                        if (*p == '"') p++;
                    }
                } else if (*p == '"' && end - p > 5 && p[1] == 't' && p[2] == 'i' &&
                           p[3] == 'e' && p[4] == 'r' && p[5] == '"') {
                    p += 7; // skip "tier":
                    tier = static_cast<uint8_t>(*p - '0');
                    if (tier > 2) tier = 1;
                    p++;
                } else if (*p == '"' && end - p > 7 && p[1] == 'e' && p[2] == 'p' &&
                           p[3] == 's' && p[4] == 'i' && p[5] == 'l' && p[6] == 'o' && p[7] == 'n') {
                    p += 10; // skip "epsilon":
                    // Parse float: [0-9]*('.'[0-9]*)?
                    float int_part = 0.0f;
                    float frac_part = 0.0f;
                    float frac_div = 1.0f;
                    bool has_digits = false;
                    while (p < end && *p >= '0' && *p <= '9') {
                        int_part = int_part * 10.0f + static_cast<float>(*p - '0');
                        has_digits = true;
                        p++;
                    }
                    if (p < end && *p == '.') {
                        p++;
                        while (p < end && *p >= '0' && *p <= '9') {
                            frac_part = frac_part * 10.0f + static_cast<float>(*p - '0');
                            frac_div *= 10.0f;
                            has_digits = true;
                            p++;
                        }
                    }
                    epsilon = has_digits ? int_part + frac_part / frac_div : 0.01f;
                } else {
                    p++;
                }
            }

            // Resolve Vars API ID
            char name_buf[256];
            if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
            std::memcpy(name_buf, name_start, name_len);
            name_buf[name_len] = '\0';

            int vars_id = resolve_var_id(name_buf, var_type);
            uint16_t name_id = compute_intern_id({name_buf, name_len});

            bridge_add_var(var_type, val_type, tier, epsilon,
                          name_id, vars_id);
        }
        p++;
    }
}

// =============================================================================
// MSFS 2024 Module Lifecycle — module_init / module_deinit
// =============================================================================
//
// These are the first and last functions called by MSFS for any WASM module.
// They register/unregister the CommBus channels for the V2 delta protocol.

extern "C" MSFS_CALLBACK void module_init(void) {
    g_module_frame_count = 0;
    g_module_initialized = false;
    g_delta_count = 0;
    frames_since_sync = 0;
    last_host_epoch = 0;

#if HAS_MSFS_SDK
    fsCommBusRegister(BridgeChannels::Frame,
                      reinterpret_cast<fsCommBusWasmCallback>(on_frame_request),
                      nullptr);
    fsCommBusRegister(BridgeChannels::Control,
                      reinterpret_cast<fsCommBusWasmCallback>(on_control_request),
                      nullptr);
#endif

    g_module_initialized = true;
}

extern "C" MSFS_CALLBACK void module_deinit(void) {
    g_module_initialized = false;

#if HAS_MSFS_SDK
    fsCommBusUnregisterAll();
#endif

    g_delta_count = 0;
    frames_since_sync = 0;
    last_host_epoch = 0;
}

// =============================================================================
// MSFS 2024 System Callbacks — Background bridge polling, no draw
// =============================================================================
//
// The system runs every frame and drives the CommBus bridge protocol.
// Referenced from systems.cfg:
//   [WASM_SYSTEM.0]
//   ModulePath=an24_bridge
//   SystemName=an24_bridge

extern "C" MSFS_CALLBACK bool an24_bridge_system_init(
    FsContext /*ctx*/, sSystemInstallData* /*pInstallData*/)
{
    g_module_frame_count = 0;
    return true;
}

extern "C" MSFS_CALLBACK bool an24_bridge_system_update(
    FsContext /*ctx*/, float /*dTime*/)
{
    g_module_frame_count++;
    // Frame handler is driven by CommBus callbacks registered in module_init.
    // No additional per-frame work needed here — the CommBus dispatcher
    // (on_frame_request) is invoked by MSFS when host sends data.
    return true;
}

extern "C" MSFS_CALLBACK bool an24_bridge_system_kill(FsContext /*ctx*/) {
    return true;
}

// =============================================================================
// MSFS 2024 Gauge Callbacks — Minimal data-conduit gauge
// =============================================================================
//
// The gauge exists only to enable panel.cfg integration. It does not render.
// Referenced from panel.cfg:
//   htmlgauge00=WasmInstrument/WasmInstrument.html?wasm_module=an24_bridge.wasm&wasm_gauge=an24_bridge,0,0,1,1

extern "C" MSFS_CALLBACK bool an24_bridge_gauge_init(
    FsContext /*ctx*/, sGaugeInstallData* pInstallData)
{
    if (!pInstallData) return false;
    pInstallData->iSizeX = GAUGE_SIZE_X;
    pInstallData->iSizeY = GAUGE_SIZE_Y;
    return true;
}

extern "C" MSFS_CALLBACK bool an24_bridge_gauge_update(
    FsContext /*ctx*/, float /*dTime*/)
{
    return true;
}

extern "C" MSFS_CALLBACK bool an24_bridge_gauge_draw(
    FsContext /*ctx*/, sGaugeDrawData* /*pDrawData*/)
{
    return true;
}

extern "C" MSFS_CALLBACK bool an24_bridge_gauge_kill(FsContext /*ctx*/) {
    return true;
}
