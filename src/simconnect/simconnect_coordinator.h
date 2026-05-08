#pragma once

#include "simconnect/simconnect_client.h"
#include "simconnect/intern_table.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

class SimConnectProvider;

/// Shared coordinator for all SimConnectProvider instances.
///
/// MSFS SimConnect supports a single session per application. When multiple
/// Documents are open, each creates a SimConnectProvider. Without coordination,
/// each provider opens its own SimConnectClient → session conflict.
///
/// The coordinator solves this by:
///   1. Owning a single SimConnectClient (shared across all providers)
///   2. Reference-counting the connection (last provider disconnect closes)
///   3. Broadcasting responses to all registered providers (each filters its own)
///   4. Maintaining a unified InternTable (same name → same name_id everywhere)
///
/// Response routing: the coordinator does not demux by epoch. Instead, it
/// broadcasts every response to all providers. Each provider checks whether the
/// response's name_ids are present in its own id_to_signal_ mapping; irrelevant
/// records are silently ignored. This is correct because:
///   - Overlapping variables: both providers get the update → correct
///   - Disjoint variables: each provider ignores the other's records → correct
///   - Pong: only the provider whose last_ping_id_ matches processes it
///
/// RegisterNames: providers no longer send their own registration. The
/// coordinator collects mappings from all providers and sends one unified
/// RegisterNames JSON when the first provider connects (or when a new provider
/// joins an already-connected session).
class SimConnectCoordinator {
public:
    using ClientFactory = std::function<std::unique_ptr<SimConnectClient>()>;

    explicit SimConnectCoordinator(ClientFactory factory = nullptr);
    ~SimConnectCoordinator();

    SimConnectCoordinator(const SimConnectCoordinator&) = delete;
    SimConnectCoordinator& operator=(const SimConnectCoordinator&) = delete;
    SimConnectCoordinator(SimConnectCoordinator&&) = delete;
    SimConnectCoordinator& operator=(SimConnectCoordinator&&) = delete;

    // ==...== Singleton ==...==

    static SimConnectCoordinator& instance();
    static void reset_instance();  ///< Test-only: destroys current singleton

    // ==...== Provider Lifecycle ==...==

    void add_provider(SimConnectProvider* provider);
    void remove_provider(SimConnectProvider* provider);
    size_t provider_count() const { return providers_.size(); }

    // ==...== Shared Resources ==...==

    InternTable& intern_table() { return intern_table_; }
    const InternTable& intern_table() const { return intern_table_; }

    // ==...== Connection (ref-counted) ==...==

    /// Open shared client on first connect; idempotent per provider.
    bool connect(SimConnectProvider* provider);

    /// Disconnect provider; close client when last provider disconnects.
    void disconnect(SimConnectProvider* provider);

    bool is_connected() const;

    // ==...== Per-frame I/O ==...==

    void poll(double elapsed_time);

    // ==...== Send via shared client ==...==

    bool send_bytes(const uint8_t* data, size_t len);
    bool send_request(const std::string& json_payload);

    // ==...== Test-only: raw client access ==...==

    std::optional<SimConnectClient*> client();

    // ==...== Test-only API ==...==

    /// Inject a client for testing (replaces factory-created client).
    void set_client_for_testing(std::unique_ptr<SimConnectClient> client);

    /// Number of active connections (ref count).
    size_t connect_count() const { return connect_count_; }

private:
    ClientFactory client_factory_;
    std::unique_ptr<SimConnectClient> client_;
    InternTable intern_table_;
    std::vector<SimConnectProvider*> providers_;
    std::unordered_set<SimConnectProvider*> connected_providers_;
    size_t connect_count_ = 0;
    bool needs_reregister_ = false;

    void on_response(std::span<const uint8_t> payload);
    void broadcast_response(std::span<const uint8_t> payload);
    void send_register_names();

    static SimConnectCoordinator* instance_;
};
