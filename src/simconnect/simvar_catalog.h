#pragma once

#include "simconnect/wire_protocol.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Central catalog of known simulation variables available for UI/UX consumption.
///
/// Combines two sources:
///   1. Bundled AVar catalog (JSON file shipped with the editor)
///   2. Live LVar enumerations from the WASM bridge (control channel)
///
/// Singleton — matches SimConnectCoordinator pattern.
class SimVarCatalog {
public:
    struct Entry {
        std::string name;
        VarType var_type;
        ValType val_type;
        std::string unit;
        std::string description;
    };

    static SimVarCatalog& instance();
    static void reset_instance();

    SimVarCatalog(const SimVarCatalog&) = delete;
    SimVarCatalog& operator=(const SimVarCatalog&) = delete;
    SimVarCatalog(SimVarCatalog&&) = delete;
    SimVarCatalog& operator=(SimVarCatalog&&) = delete;

    /// Load bundled AVars from a JSON string (for testing or embedded resources).
    /// Returns true on success.
    bool load_bundled_from_string(const std::string& json_str);

    /// Load bundled AVars from a JSON file.
    /// Returns true on success.
    bool load_bundled(const std::string& file_path);

    /// Add a single enumerated LVar.
    void add_lvar(std::string_view name, ValType val_type);

    /// Add multiple enumerated LVars at once.
    void add_lvars(const std::vector<Entry>& lvars);

    /// Remove all live LVar entries (e.g. before re-enumerating).
    void clear_lvars();

    /// Find entries matching the filter string (case-insensitive substring match).
    /// When var_type is specified, only entries of that type are returned.
    /// When filter is empty, all matching-type entries are returned.
    std::vector<Entry> find(std::string_view filter,
                            std::optional<VarType> var_type = std::nullopt) const;

    /// Total number of entries across all sources.
    size_t size() const { return avars_.size() + lvars_.size(); }

    /// Number of bundled AVar entries.
    size_t avar_count() const { return avars_.size(); }

    /// Number of live LVar entries.
    size_t lvar_count() const { return lvars_.size(); }

private:
    SimVarCatalog() = default;
    ~SimVarCatalog() = default;

    static SimVarCatalog* instance_;

    std::vector<Entry> avars_;
    std::vector<Entry> lvars_;
};
