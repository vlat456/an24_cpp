#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/// Read-only snapshot of a sim variable value.
struct SimVarValue {
    float value = 0.0f;
    bool valid = false;
};

/// Platform-agnostic SimConnect client interface.
///
/// Pure virtual — zero platform types in header.
/// Win32 implementation includes <Windows.h> and <SimConnect.h> in its .cpp only.
/// Stub implementation used for macOS/editor testing.
class SimConnectClient {
public:
    virtual ~SimConnectClient() = default;

    // ==...== Connection Lifecycle ==...==

    /// Connect to the MSFS SimConnect pipe.
    /// Returns true on success, false if MSFS is not running.
    virtual bool connect() = 0;

    /// Disconnect from SimConnect.
    virtual void disconnect() = 0;

    /// Process pending SimConnect messages (call every frame).
    /// elapsed_time: simulation time since last poll (for CommBus timeout tracking).
    virtual void poll(double elapsed_time) = 0;

    /// Check if currently connected.
    virtual bool is_connected() const = 0;

    // ==...== CommBus Communication (WASM Bridge) ==...==

    /// Send a JSON request to the WASM bridge via CommBus (control channel).
    /// Returns true if the message was queued successfully.
    virtual bool send_request(const std::string& json_payload) = 0;

    /// Send raw binary data to the WASM bridge via CommBus (frame channel).
    /// Returns true if the message was queued successfully.
    virtual bool send_bytes(const uint8_t* data, size_t len) = 0;

    /// Register a callback for WASM bridge responses.
    /// Callback receives the raw payload from CommBus (JSON or binary).
    virtual void set_response_callback(std::function<void(const std::string&)> cb) = 0;

    // ==...== Direct Sim Variable Access (A:Vars) ==...==

    /// Read a sim variable directly (bypasses WASM bridge).
    /// sim_var: MSFS variable name (e.g. "AMBIENT TEMPERATURE")
    /// index: 1-based index (MSFS convention: "ENG RPM:1")
    virtual SimVarValue read(const std::string& sim_var, int index) const = 0;

    /// Write a sim variable directly (bypasses WASM bridge).
    virtual void write(const std::string& sim_var, int index, float value) = 0;

    /// Send a key event (e.g. toggle switch, button press).
    virtual void send_event(const std::string& event_name, uint32_t data) = 0;
};

/// Factory: creates the appropriate platform implementation.
/// Stub on macOS, Win32 on Windows.
std::unique_ptr<SimConnectClient> create_simconnect_client();
