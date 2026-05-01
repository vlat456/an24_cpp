#pragma once

#include "simvar_provider.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

/// Host that owns zero or more SimVarProvider instances and orchestrates
/// the per-frame I/O loop.
///
/// Connection model:
///   - "Enabled" is a persistent user preference, toggled via the Adapters menu.
///     It survives sim start/stop/rebuild. Stored as a set of type names.
///   - Providers are created at sim start from blueprint connector nodes.
///     When built, auto-connected if their type is in the enabled set.
///   - Sim stop destroys providers but does NOT clear the enabled set.
///
/// Frame orchestration (only when providers exist and are connected):
///   poll(dt) → read_into(values, count) → [sim step] → write_from(values, count)
class SimvarProviderHost {
public:
    SimvarProviderHost() = default;
    ~SimvarProviderHost() = default;

    SimvarProviderHost(const SimvarProviderHost&) = delete;
    SimvarProviderHost& operator=(const SimvarProviderHost&) = delete;
    SimvarProviderHost(SimvarProviderHost&&) = default;
    SimvarProviderHost& operator=(SimvarProviderHost&&) = default;

    /// Factory function type — returns a new provider instance.
    using ProviderFactory = std::function<std::unique_ptr<SimVarProvider>()>;

    /// Register a provider type by name. Call once at app startup.
    /// Use concrete provider's register_type() static method for convenience.
    static void register_provider(const std::string& type, ProviderFactory factory);

    /// Scan build input, group connector nodes by ComponentKind, create and
    /// build a provider for each group. Auto-connects if type is enabled.
    void build(const JitBuildInput& input, JIT_Simulator& sim);

    /// Connect all providers. Returns true only if ALL providers connect.
    bool connect();

    /// Disconnect all providers (but keep enabled state).
    void disconnect();

    /// True if all providers are connected (i.e. ready for frame I/O).
    bool is_connected() const;

    /// Poll all providers.
    void poll(double elapsed_time);

    /// Read inputs from all providers into the signal array.
    void read_into(float* values, uint32_t count);

    /// Write outputs from the signal array to all providers.
    void write_from(const float* values, uint32_t count);

    /// Number of active provider instances (created at sim start).
    size_t provider_count() const { return providers_.size(); }

    // -- Enabled-state management (persistent user preference) --

    /// Toggle the enabled flag for a provider type. Returns new state.
    /// Only connects/disconnects providers of the specified type.
    bool toggle_enabled(const std::string& type);

    /// Check if a provider type is enabled (user wants it connected).
    bool is_enabled(const std::string& type) const;

    /// Check if a specific provider type has a connected instance.
    bool is_type_connected(const std::string& type) const;

    /// Enumerate all registered provider type names.
    /// Returns alphabetically sorted list.
    static std::vector<std::string> registered_types();

    /// Disconnect and destroy all provider instances.
    /// Does NOT clear enabled state. Called on sim stop.
    void teardown();

private:
    /// A provider instance paired with its type name.
    struct ProviderEntry {
        std::string type;
        std::unique_ptr<SimVarProvider> provider;
    };
    std::vector<ProviderEntry> providers_;

    /// Persistent set of type names the user has enabled via the menu.
    /// Survives sim start/stop/rebuild.
    std::set<std::string> enabled_types_;

    /// Look up a registered factory by type name.
    static std::unique_ptr<SimVarProvider> create_provider(const std::string& type);
};
