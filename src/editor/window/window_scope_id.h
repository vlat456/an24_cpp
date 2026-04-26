#pragma once

#include "core/strings/interned_id.h"

#include <vector>

/// Rendering mode for a BlueprintWindow.
enum class BlueprintWindowMode {
    RootDocument,       ///< Main document canvas
    EmbeddedScope,      ///< Embedded sub-blueprint filtered by embedded host id
    ExternalReference,  ///< Read-only view of external blueprint, signals mapped through parent
};

/// Typed window scope identity — pure InternedId path, zero strings.
///
/// Disambiguates root, embedded, and external-ref windows using InternedId
/// path segments. No string members, no string accessors. Display/logging
/// consumers resolve via `interner.resolve()` at the call site.
class WindowScopeId {
public:
    static WindowScopeId root() {
        return WindowScopeId(BlueprintWindowMode::RootDocument, {});
    }

    static WindowScopeId embedded(std::vector<core::InternedId> scope_path) {
        return WindowScopeId(BlueprintWindowMode::EmbeddedScope, std::move(scope_path));
    }

    static WindowScopeId external(std::vector<core::InternedId> scope_path) {
        return WindowScopeId(BlueprintWindowMode::ExternalReference, std::move(scope_path));
    }

    BlueprintWindowMode mode() const { return mode_; }
    const std::vector<core::InternedId>& path() const { return path_segments_; }

    bool operator==(const WindowScopeId& other) const {
        return mode_ == other.mode_ && path_segments_ == other.path_segments_;
    }

    bool operator!=(const WindowScopeId& other) const {
        return !(*this == other);
    }

    bool is_root() const { return mode_ == BlueprintWindowMode::RootDocument; }
    bool is_embedded() const { return mode_ == BlueprintWindowMode::EmbeddedScope; }
    bool is_external() const { return mode_ == BlueprintWindowMode::ExternalReference; }

    WindowScopeId append(core::InternedId child_segment) const {
        std::vector<core::InternedId> next = path_segments_;
        next.push_back(child_segment);
        if (is_external()) {
            return external(std::move(next));
        }
        return embedded(std::move(next));
    }

private:
    BlueprintWindowMode mode_;
    std::vector<core::InternedId> path_segments_;

    explicit WindowScopeId(BlueprintWindowMode mode, std::vector<core::InternedId> path_segments)
        : mode_(mode), path_segments_(std::move(path_segments)) {}
};
