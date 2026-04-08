#pragma once

#include <cassert>
#include <string>

/// Rendering mode for a BlueprintWindow.
enum class BlueprintWindowMode {
    RootDocument,       ///< Main document canvas
    EmbeddedGroup,      ///< Embedded sub-blueprint filtered by layout_group
    ExternalReference,  ///< Read-only view of external blueprint, signals mapped through parent
};

/// Typed window scope identity that disambiguates root, embedded, and external-ref windows.
/// Avoids implicit coupling on empty string for external-ref mode.
class WindowScopeId {
public:
    /// Construct a root window scope (no embedded group).
    static WindowScopeId root() {
        return WindowScopeId(BlueprintWindowMode::RootDocument, "");
    }

    /// Construct an embedded group scope.
    static WindowScopeId embedded(const std::string& group_id) {
        assert(!group_id.empty() && "Embedded scope requires non-empty group_id");
        return WindowScopeId(BlueprintWindowMode::EmbeddedGroup, group_id);
    }

    /// Construct an external-reference scope by parent instance ID.
    static WindowScopeId external(const std::string& parent_instance_id) {
        assert(!parent_instance_id.empty() && "External scope requires non-empty parent_instance_id");
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
    bool is_embedded() const { return mode_ == BlueprintWindowMode::EmbeddedGroup; }
    bool is_external() const { return mode_ == BlueprintWindowMode::ExternalReference; }

    /// Scope prefix for simulation signal routing.
    /// Root returns "", Embedded returns group_id, External returns parent_instance_id.
    const std::string& sim_scope_prefix() const { return key_; }

private:
    BlueprintWindowMode mode_;
    std::string key_;

    explicit WindowScopeId(BlueprintWindowMode mode, const std::string& key)
        : mode_(mode), key_(key) {}
};
