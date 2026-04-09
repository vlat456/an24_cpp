#pragma once

#include <stdexcept>
#include <string>

/// Rendering mode for a BlueprintWindow.
enum class BlueprintWindowMode {
    RootDocument,       ///< Main document canvas
    EmbeddedScope,      ///< Embedded sub-blueprint filtered by embedded host id
    ExternalReference,  ///< Read-only view of external blueprint, signals mapped through parent
};

/// Typed window scope identity that disambiguates root, embedded, and external-ref windows.
/// Avoids implicit coupling on empty string for external-ref mode.
class WindowScopeId {
public:
    /// Construct a root window scope (no embedded host scope).
    static WindowScopeId root() {
        return WindowScopeId(BlueprintWindowMode::RootDocument, "");
    }

    /// Construct an embedded scope keyed by embedded host node id.
    static WindowScopeId embedded(const std::string& scope_host_id) {
        if (scope_host_id.empty()) {
            throw std::logic_error("Embedded scope requires non-empty scope_host_id");
        }
        return WindowScopeId(BlueprintWindowMode::EmbeddedScope, scope_host_id);
    }

    /// Construct an external-reference scope by parent instance ID.
    static WindowScopeId external(const std::string& parent_instance_id) {
        if (parent_instance_id.empty()) {
            throw std::logic_error("External scope requires non-empty parent_instance_id");
        }
        return WindowScopeId(BlueprintWindowMode::ExternalReference, parent_instance_id);
    }

    BlueprintWindowMode mode() const { return mode_; }
    const std::string& key() const { return key_; }

    bool operator==(const WindowScopeId& other) const {
        return mode_ == other.mode_ && key_ == other.key_;
    }

    bool operator!=(const WindowScopeId& other) const {
        return !(*this == other);
    }

    bool is_root() const { return mode_ == BlueprintWindowMode::RootDocument; }
    bool is_embedded() const { return mode_ == BlueprintWindowMode::EmbeddedScope; }
    bool is_external() const { return mode_ == BlueprintWindowMode::ExternalReference; }

    /// Scope prefix for simulation signal routing.
    /// Root returns "", Embedded returns embedded host id, External returns
    /// parent_instance_id.
    const std::string& sim_scope_prefix() const { return key_; }

private:
    BlueprintWindowMode mode_;
    std::string key_;

    explicit WindowScopeId(BlueprintWindowMode mode, const std::string& key)
        : mode_(mode), key_(key) {}
};
