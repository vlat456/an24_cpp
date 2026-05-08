#pragma once

#include "core/solvers/jit/bridge/simvar_provider.h"
#include "simconnect/simconnect_client.h"
#include "simconnect/simconnect_mapping.h"
#include "simconnect/intern_table.h"
#include "simconnect/wire_codec.h"
#include "core/solvers/jit/jit_build_input.h"
#include "core/solvers/jit/simulator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// SimConnect adapter — implements SimVarProvider with direct MSFS 2024 integration.
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
///   3. Per-frame loop (via SimVarProvider interface):
///      a. poll()            — process messages, heartbeat
///      b. read_into()       — request inputs + inject buffered values → values[]
///      c. write_from()      — extract outputs → DeltaWrite → WASM bridge
///
/// Editor-only. Not part of WASM build.
class SimConnectProvider final : public SimVarProvider {
public:
    SimConnectProvider();
    ~SimConnectProvider() override;

    SimConnectProvider(const SimConnectProvider&) = delete;
    SimConnectProvider& operator=(const SimConnectProvider&) = delete;
    SimConnectProvider(SimConnectProvider&&) = delete;
    SimConnectProvider& operator=(SimConnectProvider&&) = delete;

    const char* name() const override { return "SimConnect"; }

    void build(const JitBuildInput& input, JIT_Simulator& /*sim*/) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    void poll(double elapsed_time) override;
    void read_into(float* values, uint32_t count) override;
    void write_from(const float* values, uint32_t count) override;
    std::optional<SignalType> signal_type(uint32_t signal_index) const override;

    /// Returns true if WASM bridge is responding to pings (alive within timeout).
    bool is_alive() const { return connection_healthy_; }

    /// Access the underlying SimConnect client (for tests that need StubClient access).
    SimConnectClient* client() { return client_.get(); }
    const SimConnectClient* client() const { return client_.get(); }

    /// Number of mapped input variables.
    size_t input_count() const { return input_mappings_.size(); }

    /// Number of mapped output variables.
    size_t output_count() const { return output_mappings_.size(); }

    /// Look up the wire value type for a given signal index.
    /// Returns Float32 if the index is not mapped.
    ValType val_type_for_signal(uint32_t signal_index) const;

    /// Register "simconnect" provider type with the host registry.
    /// Must be called before SimvarProviderHost::build().
    static void register_type();

    // ==...== Test-accessible internals ==...==

    void build_mappings(const JitBuildInput& input);
    void request_inputs();
    void inject_inputs_raw(float* values, uint32_t values_count);
    void extract_outputs_raw(const float* values, uint32_t values_count);

private:
    static std::optional<SimVarMapping> parse_mapping(
        const SolverDevice& device, const JitBuildInput& input);

    // ==...== CommBus ==...==

    void on_response(std::span<const uint8_t> payload);
    void handle_json_response(std::span<const uint8_t> payload);
    void send_bytes(const uint8_t* data, size_t len);

    // ==...== Heartbeat ==...==

    void maybe_send_ping(double current_time);
    void handle_pong(const PacketHeader& pong_hdr);

    // ==...== Members ==...==

    std::unique_ptr<SimConnectClient> client_;
    std::vector<SimVarMapping> input_mappings_;
    std::vector<SimVarMapping> output_mappings_;

    InternTable intern_table_;

    std::unordered_map<uint32_t, WireValue> input_buffer_;
    std::unordered_map<uint16_t, uint32_t> id_to_signal_;
    std::unordered_map<uint32_t, ValType> signal_to_val_type_;
    std::unordered_map<uint32_t, WireValue> output_shadow_;

    std::vector<uint8_t> send_buffer_;
    std::vector<VarRecord> output_records_;

    uint16_t host_epoch_ = 0;
    uint32_t frame_counter_ = 0;

    static constexpr double PING_INTERVAL_SEC  = 5.0;
    static constexpr double PONG_TIMEOUT_SEC   = 10.0;

    double last_ping_sent_time_   = 0.0;
    double last_pong_recv_time_   = 0.0;
    double current_time_          = 0.0;
    bool   connection_healthy_    = true;
    uint16_t next_ping_id_        = 1;
};
