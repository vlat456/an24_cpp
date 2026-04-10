#pragma once
#include "ui/core/interned_id.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace bp2 {

enum class PathKind : uint8_t {
    Root,
    Node,
    Port,
    Nested,
    Wire
};

class PathArena;

class Path {
public:
    Path() : kind_(PathKind::Root), segment_(), parent_idx_(0) {}

    PathKind kind() const { return kind_; }
    ui::InternedId segment() const { return segment_; }
    uint32_t parent_index() const { return parent_idx_; }

    bool operator==(Path other) const {
        return kind_ == other.kind_
            && segment_ == other.segment_
            && parent_idx_ == other.parent_idx_;
    }
    bool operator!=(Path other) const { return !(*this == other); }

private:
    friend class PathArena;
    Path(PathKind k, ui::InternedId seg, uint32_t parent)
        : kind_(k), segment_(seg), parent_idx_(parent) {}

    PathKind kind_;
    ui::InternedId segment_;
    uint32_t parent_idx_;
};

class PathArena {
public:
    explicit PathArena(ui::StringInterner& interner)
        : interner_(interner) {
        paths_.push_back(Path(PathKind::Root, ui::InternedId{}, 0));
    }

    Path root() const { return paths_[0]; }

    Path make_node(Path parent, ui::InternedId node_id) {
        return add_path(PathKind::Node, node_id, parent);
    }

    Path make_port(Path parent, ui::InternedId port_name) {
        return add_path(PathKind::Port, port_name, parent);
    }

    Path make_nested(Path parent, ui::InternedId instance_id) {
        return add_path(PathKind::Nested, instance_id, parent);
    }

    Path make_wire(Path parent, ui::InternedId wire_id) {
        return add_path(PathKind::Wire, wire_id, parent);
    }

    Path parent(Path p) const {
        return paths_[p.parent_index()];
    }

    std::string to_string(Path p) const;
    std::optional<Path> parse(std::string_view s);

    /// Resolve an InternedId back to its string representation.
    /// Returns empty string_view for the empty ID.
    std::string_view resolve_id(ui::InternedId id) const { return interner_.resolve(id); }

private:
    ui::StringInterner& interner_;
    std::vector<Path> paths_;

    Path add_path(PathKind kind, ui::InternedId seg, Path parent) {
        uint32_t parent_idx = index_of(parent);
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.push_back(Path(kind, seg, parent_idx));
        return paths_[idx];
    }

    uint32_t index_of(Path p) const {
        for (uint32_t i = 0; i < paths_.size(); ++i) {
            if (paths_[i] == p) return i;
        }
        return 0;
    }
};

/// Arena-independent wire endpoint: identifies a (node, port) pair
/// without binding to any PathArena.
struct WireEndpoint {
    ui::InternedId node;
    ui::InternedId port;

    bool operator==(WireEndpoint const& o) const {
        return node == o.node && port == o.port;
    }
    bool operator!=(WireEndpoint const& o) const { return !(*this == o); }

    /// Materialize a Path in the given arena (Root → Node → Port).
    Path to_path(PathArena& arena) const {
        return arena.make_port(arena.make_node(arena.root(), node), port);
    }
};

} // namespace bp2

template <>
struct std::hash<bp2::WireEndpoint> {
    size_t operator()(bp2::WireEndpoint const& ep) const noexcept {
        size_t h = std::hash<ui::InternedId>{}(ep.node);
        h ^= std::hash<ui::InternedId>{}(ep.port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template <>
struct std::hash<bp2::Path> {
    size_t operator()(bp2::Path p) const noexcept {
        size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(p.kind()));
        h ^= std::hash<ui::InternedId>{}(p.segment()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(p.parent_index()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
