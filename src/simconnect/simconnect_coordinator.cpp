#include "simconnect/simconnect_coordinator.h"
#include "simconnect/simconnect_provider.h"
#include "simconnect/wire_codec.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace {

struct VarKey {
    std::string name;
    int index;
    bool operator==(const VarKey& other) const {
        return name == other.name && index == other.index;
    }
};

struct VarKeyHash {
    size_t operator()(const VarKey& k) const {
        return std::hash<std::string>{}(k.name) ^ std::hash<int>{}(k.index);
    }
};

inline const char* val_type_name(ValType t) {
    switch (t) {
        case ValType::Float32: return "Float32";
        case ValType::Int32:   return "Int32";
        case ValType::Bool:    return "Bool";
    }
    return "Float32";
}

inline const char* mode_name(SimVarMode m) {
    switch (m) {
        case SimVarMode::Data:  return "Data";
        case SimVarMode::Event: return "Event";
    }
    return "Data";
}

} // namespace

// ==...== Singleton ==...==

SimConnectCoordinator* SimConnectCoordinator::instance_ = nullptr;

SimConnectCoordinator& SimConnectCoordinator::instance() {
    if (!instance_) {
        instance_ = new SimConnectCoordinator();
    }
    return *instance_;
}

void SimConnectCoordinator::reset_instance() {
    delete instance_;
    instance_ = nullptr;
}

// ==...== Lifecycle ==...==

SimConnectCoordinator::SimConnectCoordinator(ClientFactory factory)
    : client_factory_(std::move(factory)) {
}

SimConnectCoordinator::~SimConnectCoordinator() {
    if (client_) {
        client_->disconnect();
    }
}

// ==...== Provider Lifecycle ==...==

void SimConnectCoordinator::add_provider(SimConnectProvider* provider) {
    if (!provider) return;
    // Idempotent: remove first to avoid duplicates
    remove_provider(provider);
    providers_.push_back(provider);
    needs_reregister_ = true;
}

void SimConnectCoordinator::remove_provider(SimConnectProvider* provider) {
    if (!provider) return;
    auto it = std::find(providers_.begin(), providers_.end(), provider);
    if (it != providers_.end()) {
        providers_.erase(it);
        needs_reregister_ = true;
    }
}

// ==...== Connection ==...==

bool SimConnectCoordinator::connect(SimConnectProvider* provider) {
    if (provider && connected_providers_.count(provider)) {
        return true;  // Idempotent: already connected
    }

    if (connect_count_ == 0) {
        if (!client_) {
            if (client_factory_) {
                client_ = client_factory_();
            } else {
                client_ = create_simconnect_client();
            }
        }
        if (!client_ || !client_->connect()) {
            spdlog::error("[SimConnectCoordinator] Failed to open shared SimConnect client");
            client_.reset();
            return false;
        }
        client_->set_response_callback([this](std::span<const uint8_t> p) {
            on_response(p);
        });
        spdlog::info("[SimConnectCoordinator] Shared SimConnect client opened");
    }

    if (needs_reregister_ || connect_count_ == 0) {
        send_register_names();
        needs_reregister_ = false;
    }
    if (provider) {
        connected_providers_.insert(provider);
    }
    connect_count_++;
    return true;
}

void SimConnectCoordinator::disconnect(SimConnectProvider* provider) {
    if (provider && !connected_providers_.count(provider)) {
        return;  // Not connected
    }
    if (connect_count_ == 0) return;

    if (provider) {
        connected_providers_.erase(provider);
    }
    connect_count_--;
    if (connect_count_ == 0 && client_) {
        client_->disconnect();
        client_.reset();
        spdlog::info("[SimConnectCoordinator] Shared SimConnect client closed (last provider disconnected)");
    }
}

bool SimConnectCoordinator::is_connected() const {
    return client_ != nullptr && client_->is_connected();
}

// ==...== Per-frame I/O ==...==

void SimConnectCoordinator::poll(double elapsed_time) {
    if (client_) {
        client_->poll(elapsed_time);
    }
}

bool SimConnectCoordinator::send_bytes(const uint8_t* data, size_t len) {
    if (!client_ || !client_->is_connected()) return false;
    return client_->send_bytes(data, len);
}

bool SimConnectCoordinator::send_request(const std::string& json_payload) {
    if (!client_ || !client_->is_connected()) return false;
    return client_->send_request(json_payload);
}

// ==...== Response Handling ==...==

void SimConnectCoordinator::on_response(std::span<const uint8_t> payload) {
    broadcast_response(payload);
}

void SimConnectCoordinator::broadcast_response(std::span<const uint8_t> payload) {
    for (auto* provider : providers_) {
        if (provider) {
            provider->handle_response(payload);
        }
    }
}

// ==...== Registration ==...==

void SimConnectCoordinator::send_register_names() {
    if (!client_ || providers_.empty()) return;

    nlohmann::json j;
    j["cmd"] = "RegisterNames";
    j["version"] = 2;
    auto& vars = j["vars"] = nlohmann::json::array();

    // Collect all unique variable registrations from all providers.
    // Use a set of (name, index) tuples to avoid duplicates.
    std::unordered_set<VarKey, VarKeyHash> seen;

    for (const auto* provider : providers_) {
        if (!provider) continue;
        for (const auto& m : provider->input_mappings()) {
            VarKey key{m.var_name, m.index};
            if (seen.insert(key).second) {
                vars.push_back({{"name", m.var_name},
                                {"type", var_type_name(m.var_type)},
                                {"val_type", val_type_name(m.val_type)},
                                {"index", m.index},
                                {"unit", m.unit},
                                {"mode", mode_name(m.mode)},
                                {"tier", m.tier},
                                {"epsilon", m.epsilon}});
            }
        }
        for (const auto& m : provider->output_mappings()) {
            VarKey key{m.var_name, m.index};
            if (seen.insert(key).second) {
                vars.push_back({{"name", m.var_name},
                                {"type", var_type_name(m.var_type)},
                                {"val_type", val_type_name(m.val_type)},
                                {"index", m.index},
                                {"unit", m.unit},
                                {"mode", mode_name(m.mode)},
                                {"tier", m.tier},
                                {"epsilon", m.epsilon}});
            }
        }
    }

    std::string payload = j.dump();
    send_request(payload);
    spdlog::info("[SimConnectCoordinator] Sent unified RegisterNames ({} unique vars)", seen.size());
}

// ==...== Test-only API ==...==

std::optional<SimConnectClient*> SimConnectCoordinator::client() {
    return client_ ? std::optional<SimConnectClient*>(client_.get()) : std::nullopt;
}

void SimConnectCoordinator::set_client_for_testing(std::unique_ptr<SimConnectClient> client) {
    if (client_) {
        client_->disconnect();
    }
    client_ = std::move(client);
}
