#pragma once

#include "simconnect/simconnect_client.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

/// Stub SimConnectClient — test harness for macOS/editor.
///
/// All operations succeed but do nothing real.
/// Test-only API: set_sim_value(), written(), events_sent() for assertions.
class StubSimConnectClient final : public SimConnectClient {
public:
    bool connect() override;
    void disconnect() override;
    void poll(double elapsed_time) override;
    bool is_connected() const override;

    // CommBus
    bool send_request(const std::string& json_payload) override;
    bool send_bytes(const uint8_t* data, size_t len) override;
    void set_response_callback(std::function<void(std::span<const uint8_t>)> cb) override;

    // Direct SimVar Access
    SimVarValue read(const std::string& sim_var, int index) const override;
    void write(const std::string& sim_var, int index, float value) override;
    void send_event(const std::string& event_name, uint32_t data) override;

    // ==...== Test-Only API ==...==

    /// Set a simulated value for read() to return.
    void set_sim_value(const std::string& sim_var, int index, float value);

    /// Get the last written value for a variable.
    std::optional<float> written(const std::string& sim_var, int index) const;

    /// Get all written values (for test assertions).
    const std::map<std::string, float>& written_values() const;

    /// Get all sent events (for test assertions).
    const std::vector<std::pair<std::string, uint32_t>>& events_sent() const;

    /// Get the last JSON CommBus request payload (from send_request).
    const std::string& last_request() const;

    /// Get the last binary CommBus request payload (from send_bytes).
    const std::vector<uint8_t>& last_request_bytes() const;

    /// Trigger a mock binary response (simulates WASM bridge reply on frame channel).
    void trigger_mock_response(std::span<const uint8_t> data);

    /// Trigger a mock JSON response (simulates WASM bridge reply on control channel).
    void trigger_mock_response(const std::string& json_payload);

    /// Clear all recorded state (for test isolation).
    void reset();

    /// Make send_bytes() fail on next call (for testing failure handling).
    void set_send_bytes_should_fail(bool fail) { send_bytes_should_fail_ = fail; }

private:
    static std::string make_key(const std::string& sim_var, int index);

    bool connected_ = false;
    bool send_bytes_should_fail_ = false;
    std::map<std::string, float> sim_values_;
    std::map<std::string, float> written_;
    std::vector<std::pair<std::string, uint32_t>> events_sent_;
    std::string last_request_json_;
    std::vector<uint8_t> last_request_bytes_;
    std::function<void(std::span<const uint8_t>)> response_cb_;
};
