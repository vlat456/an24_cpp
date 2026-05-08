#include "simvar_provider_host.h"
#include "core/model/component_kind.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

// ==...== Provider Registry ==...==

/// Meyer's singleton — thread-safe initialization guaranteed by C++11.
static std::unordered_map<std::string, SimvarProviderHost::ProviderFactory>& provider_registry() {
    static std::unordered_map<std::string, SimvarProviderHost::ProviderFactory> registry;
    return registry;
}

void SimvarProviderHost::register_provider(const std::string& type, ProviderFactory factory) {
    // Guard against double-registration (editor rebuilds don't re-register).
    auto& reg = provider_registry();
    if (reg.count(type)) return;
    reg[type] = std::move(factory);
    spdlog::debug("[ProviderHost] Registered provider type '{}'", type);
}

std::unique_ptr<SimVarProvider> SimvarProviderHost::create_provider(const std::string& type) {
    auto& reg = provider_registry();
    auto it = reg.find(type);
    if (it == reg.end()) return nullptr;
    return it->second();
}

std::vector<std::string> SimvarProviderHost::registered_types() {
    auto& reg = provider_registry();
    std::vector<std::string> types;
    types.reserve(reg.size());
    for (const auto& [name, _] : reg) {
        types.push_back(name);
    }
    std::sort(types.begin(), types.end());
    return types;
}

/// Map a component kind to its provider type name.
/// Returns empty string for kinds that are not I/O connectors.
static std::string provider_type_for_kind(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::SimConnectInput:
        case ComponentKind::SimConnectOutput:
            return "simconnect";
        default:
            return "";
    }
}

// ==...== Enabled-state management ==...==

bool SimvarProviderHost::toggle_enabled(const std::string& type) {
    if (enabled_types_.count(type)) {
        enabled_types_.erase(type);
        // Disconnect only providers matching this type.
        for (auto& entry : providers_) {
            if (entry.type == type && entry.provider->is_connected()) {
                entry.provider->disconnect();
                spdlog::info("[ProviderHost] Disabled and disconnected {}", type);
            }
        }
        return false;
    } else {
        enabled_types_.insert(type);
        // Connect only providers matching this type.
        bool found = false;
        for (auto& entry : providers_) {
            if (entry.type == type) {
                found = true;
                if (!entry.provider->is_connected()) {
                    if (entry.provider->connect()) {
                        spdlog::info("[ProviderHost] Enabled and connected {}", type);
                    } else {
                        spdlog::warn("[ProviderHost] Enabled {} but connection failed", type);
                    }
                }
            }
        }
        if (!found) {
            spdlog::info("[ProviderHost] Enabled {} (no providers built yet)", type);
        }
        return true;
    }
}

bool SimvarProviderHost::is_enabled(const std::string& type) const {
    return enabled_types_.count(type) > 0;
}

bool SimvarProviderHost::is_type_connected(const std::string& type) const {
    for (const auto& entry : providers_) {
        if (entry.type == type) {
            return entry.provider->is_connected();
        }
    }
    return false;
}

void SimvarProviderHost::teardown() {
    for (auto& entry : providers_) {
        if (entry.provider->is_connected()) {
            entry.provider->disconnect();
        }
    }
    providers_.clear();
}

// ==...== Setup ==...==

void SimvarProviderHost::build(const JitBuildInput& input, JIT_Simulator& sim) {
    // Destroy old providers — they belong to the previous sim instance.
    teardown();

    // Group SimVar devices by provider type (derived from ComponentKind)
    std::unordered_map<std::string, std::vector<SolverDevice>> groups;
    for (const auto& device : input.devices) {
        std::string const ptype = provider_type_for_kind(device.kind);
        if (ptype.empty()) continue;  // Not an I/O connector
        groups[ptype].push_back(device);
    }

    for (auto& [type, devices] : groups) {
        auto provider = create_provider(type);
        if (!provider) {
            spdlog::warn("[ProviderHost] Unknown provider '{}', skipping {} devices",
                         type, devices.size());
            continue;
        }

        // Build a filtered input: same signal mapping, only this group's devices
        JitBuildInput filtered;
        filtered.signal_key_interner = input.signal_key_interner;
        filtered.port_to_signal = input.port_to_signal;
        filtered.signal_count = input.signal_count;
        filtered.initial_values = input.initial_values;
        filtered.bridge_ports = input.bridge_ports;
        filtered.devices = std::move(devices);

        provider->build(filtered, sim);

        // Auto-connect if user has enabled this type.
        if (is_enabled(type)) {
            if (provider->connect()) {
                spdlog::info("[ProviderHost] Auto-connected enabled adapter '{}'", type);
            } else {
                spdlog::warn("[ProviderHost] Enabled adapter '{}' failed to connect", type);
            }
        }

        providers_.push_back(ProviderEntry{type, std::move(provider)});
    }

    spdlog::info("[ProviderHost] Built {} provider(s)", providers_.size());
}

// ==...== Connection Lifecycle ==...==

bool SimvarProviderHost::connect() {
    bool all_ok = true;
    for (auto& entry : providers_) {
        if (!entry.provider->connect()) {
            spdlog::error("[ProviderHost] {} failed to connect",
                          entry.provider->name());
            all_ok = false;
        }
    }
    return all_ok;
}

void SimvarProviderHost::disconnect() {
    for (auto& entry : providers_) {
        if (entry.provider->is_connected()) {
            entry.provider->disconnect();
        }
    }
}

bool SimvarProviderHost::is_connected() const {
    for (const auto& entry : providers_) {
        if (!entry.provider->is_connected()) {
            return false;
        }
    }
    return !providers_.empty();
}

// ==...== Per-frame I/O ==...==

void SimvarProviderHost::poll(double elapsed_time) {
    for (auto& entry : providers_) {
        entry.provider->poll(elapsed_time);
    }
}

void SimvarProviderHost::read_into(float* values, uint32_t count) {
    for (auto& entry : providers_) {
        entry.provider->read_into(values, count);
    }
}

void SimvarProviderHost::write_from(const float* values, uint32_t count) {
    for (auto& entry : providers_) {
        entry.provider->write_from(values, count);
    }
}
