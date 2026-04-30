// =============================================================================
// AN-24 WASM Bridge Module — MSFS 2024
// =============================================================================
//
// Runs inside MSFS 2024 as a WASM module. Receives V2 delta protocol packets
// from SimConnect (host), reads/writes sim variables via Vars API, and sends
// delta/full-sync responses back via CommBus.
//
// V2 protocol: WASM maintains a shadow buffer with last-sent values per
// variable. On DeltaRead, it reads requested tiers from the Vars API and
// responds with only changed records (DeltaUpdate) or all records (FullSync).
//
// Build: wasm32-unknown-emscripten toolchain with MSFS 2024 WASM SDK.
// Entry: MSFS_CALLBACK module_init / module_deinit

// Host-side headers — available via CMake include paths in WASM build
#include "simconnect/wire_protocol.h"
#include "simconnect/wire_codec.h"
#include "simconnect/intern_table.h"

// MSFS 2024 WASM SDK headers (available only in WASM build)
// #include <MSFS/MSFS.h>
// #include <MSFS/MSFS_Vars.h>
// #include <MSFS/MSFS_CommBus.h>

#include <cstring>

// =============================================================================
// Shadow buffer — tracks last-sent value per variable for delta detection
// =============================================================================

struct DeltaEntry {
    WireValue last_sent;     ///< Last value sent to host
    float     epsilon;       ///< Change detection threshold for this variable
    uint8_t   tier;          ///< TIER_FAST/MEDIUM/SLOW
    VarType   var_type;      ///< For dispatching to correct Vars API getter
    ValType   val_type;      ///< For change detection
    bool      valid;         ///< Has been read at least once
};

static constexpr uint16_t MAX_VARS = 512;
static DeltaEntry delta_entries[MAX_VARS];
static uint16_t delta_count = 0;
static uint16_t frames_since_sync = 0;
static uint16_t last_host_epoch = 0;
static double last_ping_time = 0.0;

// =============================================================================
// Module State (static — zero heap allocation on hot path)
// =============================================================================

static uint8_t send_buffer[MAX_PACKET_SIZE];
static VarRecord response_records[MAX_VARS];

static InternTable intern_table;
static WireCodec codec;

// =============================================================================
// Frame Handler — V2 delta protocol
// =============================================================================

static void on_frame_request(const char* payload, size_t size) {
    if (size < sizeof(PacketHeader)) return;

    const auto* data = reinterpret_cast<const uint8_t*>(payload);
    auto result = codec.parse(data, size);
    if (!result.header) return;

    PacketHeader hdr = *result.header;

    switch (header_cmd(hdr)) {
        case Cmd::DeltaRead: {
            uint16_t tier_mask = hdr.count;  // count field = tier mask for DeltaRead

            // Epoch gap detection: if gap > 1, force full sync
            bool force_full = (tier_mask == TIER_MASK_FORCE_FULL_SYNC);
            if (hdr.seq_id > 0 && last_host_epoch > 0) {
                uint16_t gap = hdr.seq_id - last_host_epoch;
                if (gap > 1) force_full = true;
            }
            last_host_epoch = hdr.seq_id;

            frames_since_sync++;

            // Full sync: every FULL_SYNC_INTERVAL frames or on forced full sync
            if (force_full || frames_since_sync >= FULL_SYNC_INTERVAL) {
                frames_since_sync = 0;

                // Read all variables and send FullSync
                uint16_t count = (delta_count > MAX_VARS) ? MAX_VARS : delta_count;
                for (uint16_t i = 0; i < count; ++i) {
                    response_records[i].var_type = delta_entries[i].var_type;
                    response_records[i].val_type = delta_entries[i].val_type;
                    response_records[i].name_id  = 0;  // TODO: store intern_id
                    // TODO: Read from Vars API based on var_type
                    response_records[i].value = WireValue(0.0f);
                    delta_entries[i].last_sent = response_records[i].value;
                    delta_entries[i].valid = true;
                }

                size_t resp_size = codec.build_full_sync(
                    send_buffer, MAX_PACKET_SIZE,
                    {response_records, count}, hdr.seq_id);
                if (resp_size > 0) {
                    // TODO: fsCommBusCall(BridgeChannels::Frame, send_buffer, resp_size, 0);
                    (void)resp_size;
                }
            } else {
                // Delta update: only send changed records for requested tiers
                uint16_t changed_count = 0;
                for (uint16_t i = 0; i < delta_count; ++i) {
                    // Check tier filter
                    uint8_t tier_bit = 1u << delta_entries[i].tier;
                    if ((tier_mask & tier_bit) == 0) continue;

                    // Read current value from Vars API
                    WireValue current;
                    // TODO: Read from Vars API based on var_type
                    current = WireValue(0.0f);

                    // Change detection
                    if (delta_entries[i].valid &&
                        !value_changed(current, delta_entries[i].last_sent,
                                       delta_entries[i].epsilon, delta_entries[i].val_type)) {
                        continue;  // No change
                    }

                    response_records[changed_count].var_type = delta_entries[i].var_type;
                    response_records[changed_count].val_type = delta_entries[i].val_type;
                    response_records[changed_count].name_id  = 0;  // TODO: store intern_id
                    response_records[changed_count].value    = current;
                    delta_entries[i].last_sent = current;
                    delta_entries[i].valid = true;
                    changed_count++;
                }

                if (changed_count > 0) {
                    size_t resp_size = codec.build_delta_update(
                        send_buffer, MAX_PACKET_SIZE,
                        {response_records, changed_count}, hdr.seq_id);
                    if (resp_size > 0) {
                        // TODO: fsCommBusCall(BridgeChannels::Frame, send_buffer, resp_size, 0);
                        (void)resp_size;
                    }
                }
            }
            break;
        }

        case Cmd::Ping: {
            // Respond immediately with Pong, echoing the ping's seq_id
            size_t resp_size = codec.build_pong(
                send_buffer, MAX_PACKET_SIZE, hdr.seq_id);
            if (resp_size > 0) {
                // TODO: fsCommBusCall(BridgeChannels::Frame, send_buffer, resp_size, 0);
                (void)resp_size;
            }
            break;
        }

        case Cmd::DeltaWrite: {
            for (const auto& rec : result.records) {
                // TODO: Write to MSFS Vars API based on var_type:
                //   switch (rec.var_type) {
                //       case VarType::AVar:  fsVarsAVarSet(lookup_id, rec.value.f32); break;
                //       case VarType::LVar:  fsVarsLVarSet(lookup_id, rec.value.f32); break;
                //       case VarType::HEvent: fsEventsHEventCall(name, rec.value.f32); break;
                //       case VarType::BVar:  fsVarsBVarSet(lookup_id, rec.value.f32); break;
                //       ...
                //   }
                (void)rec;
            }

            // Send write ack
            size_t resp_size = codec.build_write_ack(
                send_buffer, MAX_PACKET_SIZE, hdr.seq_id);
            if (resp_size > 0) {
                // TODO: fsCommBusCall(BridgeChannels::Frame, send_buffer, resp_size, 0);
                (void)resp_size;
            }
            break;
        }

        default:
            break;
    }
}

// =============================================================================
// Control Handler — JSON setup channel (one-time registration)
// =============================================================================

static void on_control_request(const char* payload, size_t size) {
    // TODO: Parse JSON registration request with tier + epsilon:
    //   {"cmd":"register_names","names":[
    //     {"name":"X","var_type":"AVar","tier":0,"epsilon":0.01}, ...
    //   ]}
    // For each entry:
    //   1. Call fsVarsGetAVarId(name) / fsVarsGetLVarId(name) etc.
    //   2. Store in delta_entries[] with tier, epsilon, var_type, val_type
    //   3. Intern in intern_table
    (void)payload;
    (void)size;
}

// =============================================================================
// Module Lifecycle
// =============================================================================

extern "C" void MSFS_CALLBACK module_init(void* user_data) {
    (void)user_data;
    // TODO: Register CommBus callbacks:
    //   fsCommBusRegister(BridgeChannels::Frame, on_frame_request, nullptr);
    //   fsCommBusRegister(BridgeChannels::Control, on_control_request, nullptr);
}

extern "C" void MSFS_CALLBACK module_deinit(void* user_data) {
    (void)user_data;
    // TODO: Unregister CommBus callbacks
    intern_table.clear();
    delta_count = 0;
    frames_since_sync = 0;
    last_host_epoch = 0;
}
