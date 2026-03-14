#pragma once

/// InternedId + StringInterner: high-performance string interning for IDs.
///
/// InternedId is a trivially-copyable 4-byte wrapper around uint32_t.
/// Equality and hashing reduce to integer operations — no string work.
///
/// StringInterner maps strings to InternedIds. Interned strings are never freed,
/// so resolved string_views remain valid for the lifetime of the interner.

#include <cstdint>
#include <string>
#include <string_view>
#include <deque>
#include <vector>
#include <unordered_map>
#include <functional>

namespace ui {

// =============================================================================
// InternedId
// =============================================================================

/// Lightweight ID type backed by a uint32_t index.
/// Default-constructed value (0) represents "empty / no ID".
class InternedId {
public:
    constexpr InternedId() noexcept : value_(0) {}
    constexpr explicit InternedId(uint32_t v) noexcept : value_(v) {}

    constexpr bool empty() const noexcept { return value_ == 0; }
    constexpr uint32_t raw() const noexcept { return value_; }

    constexpr bool operator==(InternedId o) const noexcept { return value_ == o.value_; }
    constexpr bool operator!=(InternedId o) const noexcept { return value_ != o.value_; }
    constexpr bool operator<(InternedId o) const noexcept { return value_ < o.value_; }

private:
    uint32_t value_;
};

static_assert(std::is_trivially_copyable_v<InternedId>);
static_assert(sizeof(InternedId) == sizeof(uint32_t));

} // namespace ui

// std::hash specialization (must be in global namespace)
template <>
struct std::hash<ui::InternedId> {
    size_t operator()(ui::InternedId id) const noexcept {
        return std::hash<uint32_t>{}(id.raw());
    }
};

namespace ui {

// =============================================================================
// StringInterner
// =============================================================================

/// Maps strings to unique InternedId values. Thread-unsafe (single-threaded use).
///
/// Interned strings are stored in a std::deque<std::string>, which guarantees
/// pointer/reference stability on push_back — resolved string_views never
/// dangle as long as the interner is alive.
///
/// ID 0 is reserved as the "empty" sentinel and is never assigned.
class StringInterner {
public:
    StringInterner() = default;

    /// Intern a string. Returns its unique InternedId.
    /// Empty strings map to the empty InternedId (value 0).
    /// Duplicate strings return the same InternedId.
    InternedId intern(std::string_view str) {
        if (str.empty()) return InternedId{};

        auto it = index_.find(str);
        if (it != index_.end()) {
            return InternedId{it->second};
        }

        // New string: store it and assign the next ID.
        // IDs are 1-based (0 = empty sentinel).
        uint32_t new_id = static_cast<uint32_t>(strings_.size()) + 1;
        strings_.emplace_back(str);

        // The string_view key points into the stable deque storage.
        std::string_view stable_view = strings_.back();
        index_.emplace(stable_view, new_id);
        reverse_.push_back(stable_view);

        return InternedId{new_id};
    }

    /// Resolve an InternedId back to its string.
    /// Returns empty string_view for the empty ID.
    std::string_view resolve(InternedId id) const {
        if (id.empty()) return {};
        uint32_t idx = id.raw() - 1;  // 1-based → 0-based
        if (idx >= reverse_.size()) return {};
        return reverse_[idx];
    }

    /// Number of unique strings currently interned.
    size_t size() const noexcept { return strings_.size(); }

    /// Check if a string has already been interned.
    bool contains(std::string_view str) const {
        return index_.find(str) != index_.end();
    }

    /// Look up an already-interned string. Returns empty InternedId if not found.
    /// Unlike intern(), this is const and never creates new entries.
    InternedId lookup(std::string_view str) const {
        if (str.empty()) return InternedId{};
        auto it = index_.find(str);
        if (it != index_.end()) return InternedId{it->second};
        return InternedId{};
    }

private:
    /// Stable storage for interned strings. deque guarantees that
    /// existing elements are not relocated on push_back.
    std::deque<std::string> strings_;

    /// Forward index: string_view (pointing into strings_) → 1-based ID.
    std::unordered_map<std::string_view, uint32_t> index_;

    /// Reverse index: 0-based (id - 1) → string_view into strings_.
    std::vector<std::string_view> reverse_;
};

} // namespace ui
