#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
        return WindowScopeId(BlueprintWindowMode::RootDocument, {});
    }

    /// Construct an embedded scope keyed by embedded host node id.
    static WindowScopeId embedded(std::string_view scope_host_id) {
        if (scope_host_id.empty()) {
            throw std::logic_error("Embedded scope requires non-empty scope_host_id");
        }
        return WindowScopeId(BlueprintWindowMode::EmbeddedScope, {std::string(scope_host_id)});
    }

    /// Construct an embedded scope from a full nested instance path.
    static WindowScopeId embedded(std::vector<std::string> scope_path) {
        if (scope_path.empty()) {
            throw std::logic_error("Embedded scope requires non-empty scope path");
        }
        return WindowScopeId(BlueprintWindowMode::EmbeddedScope, std::move(scope_path));
    }

    /// Construct an external-reference scope by parent instance ID.
    static WindowScopeId external(std::string_view parent_instance_id) {
        if (parent_instance_id.empty()) {
            throw std::logic_error("External scope requires non-empty parent_instance_id");
        }
        return WindowScopeId(BlueprintWindowMode::ExternalReference, {std::string(parent_instance_id)});
    }

    /// Construct an external-reference scope from a full nested instance path.
    static WindowScopeId external(std::vector<std::string> scope_path) {
        if (scope_path.empty()) {
            throw std::logic_error("External scope requires non-empty scope path");
        }
        return WindowScopeId(BlueprintWindowMode::ExternalReference, std::move(scope_path));
    }

    BlueprintWindowMode mode() const { return mode_; }
    const std::vector<std::string>& path() const { return path_segments_; }

    const std::string& key() const {
        if (path_segments_.empty()) {
            throw std::logic_error("Root scope has no key()");
        }
        return path_segments_.back();
    }

    bool operator==(const WindowScopeId& other) const {
        return mode_ == other.mode_ && path_segments_ == other.path_segments_;
    }

    bool operator!=(const WindowScopeId& other) const {
        return !(*this == other);
    }

    bool is_root() const { return mode_ == BlueprintWindowMode::RootDocument; }
    bool is_embedded() const { return mode_ == BlueprintWindowMode::EmbeddedScope; }
    bool is_external() const { return mode_ == BlueprintWindowMode::ExternalReference; }

    WindowScopeId append(std::string_view child_segment) const {
        if (child_segment.empty()) {
            throw std::logic_error("WindowScopeId::append requires non-empty child segment");
        }

        std::vector<std::string> next = path_segments_;
        next.emplace_back(child_segment);
        if (is_external()) {
            return external(std::move(next));
        }
        return embedded(std::move(next));
    }

    /// Scope prefix for simulation signal routing.
    /// Root returns "", non-root scopes join all path segments with ':'.
    /// Pre-computed at construction — zero allocation per call.
    const std::string& sim_scope_prefix() const { return cached_prefix_; }

private:
    BlueprintWindowMode mode_;
    std::vector<std::string> path_segments_;
    std::string cached_prefix_;

    explicit WindowScopeId(BlueprintWindowMode mode, std::vector<std::string> path_segments)
        : mode_(mode), path_segments_(std::move(path_segments)), cached_prefix_(compute_prefix(path_segments_)) {}

    static std::string compute_prefix(const std::vector<std::string>& segments) {
        if (segments.empty()) return "";
        std::string joined = segments.front();
        for (size_t i = 1; i < segments.size(); ++i) {
            joined.push_back(':');
            joined += segments[i];
        }
        return joined;
    }
};
