#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

namespace detail {
struct Hasher {
    size_t h = 0;
    void combine(size_t v)   { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); }
    void hash_float(float f) { uint32_t b; std::memcpy(&b, &f, 4); combine(std::hash<uint32_t>{}(b)); }
    void hash_id(core::InternedId id) { combine(std::hash<uint32_t>{}(id.raw())); }
    size_t finish() const { return h; }
};
} // namespace detail

inline size_t blueprint_checksum(const bp2::Blueprint& bp) {
    detail::Hasher g;
    g.combine(bp.nodes().size());
    g.combine(bp.wires().size());
    // Note: nested() API removed in blueprint-instance model;
    // blueprint instances are nodes with embedded source.
    size_t ns = 0;
    for (auto& n : bp.nodes()) {
         detail::Hasher h; h.hash_id(n.semantic.id); h.hash_float(n.layout.x); h.hash_float(n.layout.y);
        ns += h.finish();
    }
    g.combine(ns);
    size_t ws = 0;
    for (auto& w : bp.wires()) {
        detail::Hasher h; h.hash_id(w.id);
        ws += h.finish();
    }
    g.combine(ws);
    return g.finish();
}

#define VERIFY_UNDO_ROUNDTRIP(model, cmd) ((void)0)
