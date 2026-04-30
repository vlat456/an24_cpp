#pragma once

#include "simconnect_client.h"
#include "simconnect_mapping.h"
#include "intern_table.h"
#include "wire_codec.h"
#include "core/solvers/jit/jit_build_input.h"
#include "core/solvers/jit/simulator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// SimConnectBridge — connects the JIT simulator to MSFS 2024 via SimConnect + CommBus.
///
/// V2 Delta Wire Protocol: tier-based polling, shadow-buffer change detection.
/// Instead of sending all variable values every frame, the host sends an 8-byte
/// DeltaRead with a tier bitmask. The WASM bridge responds with DeltaUpdate
/// (only changed records) or FullSync (all records, periodic safety net).
///
/// Wire protocol: binary packed structs on the frame channel (An24Bridge_Frame).
/// Setup/registration: JSON on the control channel (An24Bridge_Control).
///
/// Data flow:
///   1. build_mappings() — scan JitBuildInput, intern var names, resolve signal indices
///   2. connect() — open SimConnect pipe, register vars with WASM bridge
///   3. Per-frame loop:
///      a. request_inputs()  — 8-byte DeltaRead (tier mask) → WASM bridge
///      b. poll()            — process DeltaUpdate/FullSync from WASM bridge
///      c. inject_inputs()   — buffered values → apply_typed_overrides() → values[]
///      d. Simulator::step()
///      e. extract_outputs() — DeltaWrite (only changed outputs) → WASM bridge
///
/// Editor-only. Not part of WASM build.
class SimConnectBridge {
public:
    SimConnectBridge();
    ~SimConnectBridge();

    SimConnectBridge(const SimConnectBridge&) = delete;
    SimConnectBridge& operator=(const SimConnectBridge&) = delete;
    // Move deleted: connect() registers a lambda capturing `this`.
    // After move, the callback would point to a dead object.
    SimConnectBridge(SimConnectBridge&&) = delete;
    SimConnectBridge& operator=(SimConnectBridge&&) = delete;

    // ==...== Setup ==...==

    /// Scan build input for SimVarInput/SimVarOutput nodes and build signal mappings.
    /// Interns all variable names for consistent binary IDs.
    /// Must be called after Simulator::start() so signal indices are resolved.
    void build_mappings(const JitBuildInput& input, JIT_Simulator& sim);

    // ==...== Connection Lifecycle ==...==

    bool connect();
    void disconnect();
    bool is_connected() const;

    /// Returns true if WASM bridge is responding to pings (alive within timeout).
    bool is_alive() const { return connection_healthy_; }

    /// Process pending SimConnect messages (call every frame before inject_inputs).
    /// Also handles periodic heartbeat ping.
    void poll(double elapsed_time);

    // ==...== Simulation Integration ==...==

    /// Send 8-byte DeltaRead with tier mask for current frame.
    /// WASM bridge responds with DeltaUpdate or FullSync.
    /// Tier mask varies: Tier0 every frame, Tier1 every 5th, Tier2 every 30th.
    void request_inputs();

    /// Inject buffered input values into the simulator via apply_typed_overrides().
    /// Call after poll(), before Simulator::step().
    void inject_inputs(JIT_Simulator& sim);

    /// Extract output signal values and send DeltaWrite to WASM bridge.
    /// Only outputs whose value changed beyond epsilon are included.
    /// All changed outputs packed into a single packet per frame.
    void extract_outputs(const JIT_Simulator& sim);

    // ==...== Accessors ==...==

    size_t input_count() const { return input_mappings_.size(); }
    size_t output_count() const { return output_mappings_.size(); }
    SimConnectClient* client() { return client_.get(); }
    const SimConnectClient* client() const { return client_.get(); }

private:
    /// Parse a single device's params into a SimVarMapping.
    static std::optional<SimVarMapping> parse_mapping(
        const SolverDevice& device, const JitBuildInput& input);

    /// Handle a CommBus response — detects binary vs JSON and dispatches.
    void on_response(const std::string& payload);

    /// Handle a JSON control-channel response.
    void handle_json_response(const std::string& payload);

    /// Send raw bytes through CommBus (wraps as string for client interface).
    void send_bytes(const uint8_t* data, size_t len);

    std::unique_ptr<SimConnectClient> client_;
    std::vector<SimVarMapping> input_mappings_;
    std::vector<SimVarMapping> output_mappings_;

    /// String interning — consistent name→ID across sessions.
    InternTable intern_table_;

    /// Binary codec — zero-allocation packet builder/parser.
    WireCodec codec_;

    /// Buffered input values: signal_index → latest value from WASM bridge.
    std::unordered_map<uint32_t, float> input_buffer_;

    /// Reverse lookup: interned ID → signal_index for ReadResponse parsing.
    std::unordered_map<uint16_t, uint32_t> id_to_signal_;

    /// Override list for apply_typed_overrides() — pre-allocated, reused each frame.
    std::vector<std::pair<core::InternedId, float>> overrides_;

    /// Shadow buffer for epsilon-based output change detection.
    std::unordered_map<uint32_t, WireValue> output_shadow_;

    /// Pre-allocated buffer for building binary packets.
    std::vector<uint8_t> send_buffer_;

    /// Pre-allocated record buffer for building per-frame delta write packets.
    std::vector<VarRecord> output_records_;

    /// V2 delta protocol frame tracking.
    uint16_t host_epoch_ = 0;      ///< Monotonically increasing per-frame epoch
    uint32_t frame_counter_ = 0;   ///< Frame counter for tier scheduling

    // ==...== Heartbeat / keepalive ==...==

    /// Send a Ping if enough time has elapsed since last ping.
    void maybe_send_ping(double current_time);

    /// Handle a received Pong — resets connection health timer.
    void handle_pong(const PacketHeader& pong_hdr);

    static constexpr double PING_INTERVAL_SEC  = 5.0;   ///< Send ping every 5 seconds
    static constexpr double PONG_TIMEOUT_SEC   = 10.0;  ///< No pong for 10s = unhealthy

    double last_ping_sent_time_   = 0.0;  ///< When we last sent a Ping
    double last_pong_recv_time_   = 0.0;  ///< When we last received a Pong
    double current_time_          = 0.0;  ///< Last elapsed_time from poll()
    bool   connection_healthy_    = true; ///< False if WASM bridge stopped responding
    uint16_t next_ping_id_        = 1;    ///< Monotonically increasing ping ID
};
