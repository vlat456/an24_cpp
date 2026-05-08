#include "simconnect_client_stub.h"

#include <utility>

// ==...== Connection Lifecycle ==...==

bool StubSimConnectClient::connect() {
    connected_ = true;
    return true;
}

void StubSimConnectClient::disconnect() {
    connected_ = false;
}

void StubSimConnectClient::poll(double /*elapsed_time*/) {
    // No-op in stub. Timeline support can be added later for replay testing.
}

bool StubSimConnectClient::is_connected() const {
    return connected_;
}

// ==...== CommBus ==...==

bool StubSimConnectClient::send_request(const std::string& json_payload) {
    last_request_json_ = json_payload;
    return true;
}

bool StubSimConnectClient::send_bytes(const uint8_t* data, size_t len) {
    last_request_bytes_.assign(data, data + len);
    return true;
}

void StubSimConnectClient::set_response_callback(std::function<void(std::span<const uint8_t>)> cb) {
    response_cb_ = std::move(cb);
}

void StubSimConnectClient::trigger_mock_response(std::span<const uint8_t> data) {
    if (response_cb_) {
        response_cb_(data);
    }
}

void StubSimConnectClient::trigger_mock_response(const std::string& json_payload) {
    trigger_mock_response(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(json_payload.data()), json_payload.size()));
}

// ==...== Direct SimVar Access ==...==

SimVarValue StubSimConnectClient::read(const std::string& sim_var, int index) const {
    auto key = make_key(sim_var, index);
    auto it = sim_values_.find(key);
    if (it != sim_values_.end()) {
        return {it->second, true};
    }
    return {0.0f, false};
}

void StubSimConnectClient::write(const std::string& sim_var, int index, float value) {
    written_[make_key(sim_var, index)] = value;
}

void StubSimConnectClient::send_event(const std::string& event_name, uint32_t data) {
    events_sent_.push_back({event_name, data});
}

// ==...== Test-Only API ==...==

void StubSimConnectClient::set_sim_value(const std::string& sim_var, int index, float value) {
    sim_values_[make_key(sim_var, index)] = value;
}

std::optional<float> StubSimConnectClient::written(const std::string& sim_var, int index) const {
    auto key = make_key(sim_var, index);
    auto it = written_.find(key);
    if (it != written_.end()) {
        return it->second;
    }
    return std::nullopt;
}

const std::map<std::string, float>& StubSimConnectClient::written_values() const {
    return written_;
}

const std::vector<std::pair<std::string, uint32_t>>& StubSimConnectClient::events_sent() const {
    return events_sent_;
}

const std::string& StubSimConnectClient::last_request() const {
    return last_request_json_;
}

const std::vector<uint8_t>& StubSimConnectClient::last_request_bytes() const {
    return last_request_bytes_;
}

void StubSimConnectClient::reset() {
    sim_values_.clear();
    written_.clear();
    events_sent_.clear();
    last_request_json_.clear();
    last_request_bytes_.clear();
    response_cb_ = nullptr;
    // Note: connected_ is NOT reset — connection state is orthogonal to data state.
    // Call disconnect() explicitly to reset connection state.
}

std::string StubSimConnectClient::make_key(const std::string& sim_var, int index) {
    return sim_var + ":" + std::to_string(index);
}

std::unique_ptr<SimConnectClient> create_simconnect_client() {
    return std::make_unique<StubSimConnectClient>();
}
