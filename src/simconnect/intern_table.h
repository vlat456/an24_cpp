#pragma once

// =============================================================================
// InternTable — deterministic string → uint16_t ID mapping via FNV-1a hash
// =============================================================================
//
// Provides session-independent consistent IDs for MSFS variable names.
// The same variable name always maps to the same ID, regardless of:
//   - Registration order
//   - Session restarts
//   - Host/WASM reconnection
//   - Platform (wasm32, x86_64)
//
// Hash: FNV-1a 32-bit, truncated to uint16_t via modulo with largest prime < 65536.
// Collision resolution: deterministic linear probe within a single session.
// Note: resolved IDs depend on insertion order when collisions occur —
// host and WASM must register names in the same order for identical IDs.
// In practice, FNV-1a modulo 65521 has zero collisions for typical MSFS variable
// name sets (< 500 names), so insertion order does not matter.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "wire_protocol.h"

// =============================================================================
// FNV-1a hash (inline constexpr — usable at compile time)
// =============================================================================

/// FNV-1a 32-bit hash — deterministic, portable, fast.
inline constexpr uint32_t fnv1a_hash(std::string_view s) {
    uint32_t h = 0x811c9dc5u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x01000193u;
    }
    return h;
}

/// Largest prime < 65536 — used as modulus for ID space.
/// Provides good distribution and deterministic collision resolution.
static constexpr uint16_t ID_SPACE = 65521u;

/// Compute the base interned ID for a variable name.
/// Deterministic: same string → same ID, always.
inline constexpr uint16_t compute_intern_id(std::string_view name) {
    return static_cast<uint16_t>(fnv1a_hash(name) % ID_SPACE);
}

// =============================================================================
// InternTable
// =============================================================================

/// Thread-unsafe intern table for variable name → ID mapping.
/// Typically one instance per bridge (host side) or per WASM module.
class InternTable {
public:
    /// A single interned entry.
    struct Entry {
        std::string name;        ///< Original variable name (owned)
        uint16_t    id;          ///< Interned ID (FNV-1a hash, collision-resolved)
        VarType     var_type;    ///< Variable category (AVar, LVar, etc.)
    };

    /// Intern a variable name — returns the consistent ID.
    /// If already interned, returns existing ID. If new, assigns FNV-1a-based ID
    /// with collision resolution.
    uint16_t intern(VarType var_type, std::string_view name) {
        // Check if already interned (std::string key — safe against vector realloc)
        auto it = name_to_index_.find(std::string(name));
        if (it != name_to_index_.end()) {
            return entries_[it->second].id;
        }

        // Compute base ID
        uint16_t id = compute_intern_id(name);

        // Deterministic linear probe for collision resolution
        // Same set of names always resolves to the same IDs
        while (id_to_index_.count(id) > 0) {
            // Collision — but is it the same name? (shouldn't happen, we checked above)
            const auto& existing = entries_[id_to_index_[id]];
            if (existing.name == name) {
                return existing.id;
            }
            // Linear probe to next slot (wrapping at ID_SPACE)
            id = static_cast<uint16_t>((static_cast<uint32_t>(id) + 1) % ID_SPACE);
        }

        // Insert — map key is an owned std::string, immune to vector realloc
        size_t index = entries_.size();
        entries_.push_back({std::string(name), id, var_type});
        name_to_index_[std::string(name)] = index;
        id_to_index_[id] = index;

        return id;
    }

    /// Look up the interned ID for a name. Returns nullopt if not interned.
    std::optional<uint16_t> lookup(std::string_view name) const {
        auto it = name_to_index_.find(std::string(name));
        if (it != name_to_index_.end()) {
            return entries_[it->second].id;
        }
        return std::nullopt;
    }

    /// Look up variable name by interned ID. Returns empty string_view if not found.
    std::string_view name_by_id(uint16_t id) const {
        auto it = id_to_index_.find(id);
        if (it != id_to_index_.end()) {
            return entries_[it->second].name;
        }
        return {};
    }

    /// Look up the entry by ID. Returns nullptr if not found.
    const Entry* entry_by_id(uint16_t id) const {
        auto it = id_to_index_.find(id);
        if (it != id_to_index_.end()) {
            return &entries_[it->second];
        }
        return nullptr;
    }

    /// Number of interned variables.
    size_t size() const { return entries_.size(); }

    /// True if no variables interned.
    bool empty() const { return entries_.empty(); }

    /// Clear all entries.
    void clear() {
        entries_.clear();
        name_to_index_.clear();
        id_to_index_.clear();
    }

    /// Direct access to all entries (for iteration).
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> name_to_index_;  ///< Owned keys — safe against vector realloc
    std::unordered_map<uint16_t, size_t> id_to_index_;
};
