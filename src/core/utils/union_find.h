#pragma once

#include <cstdint>
#include <vector>

namespace core::utils {

class UnionFind {
public:
    explicit UnionFind(size_t size) : parent_(size), rank_(size, 0) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
            parent_[i] = i;
        }
    }

    size_t size() const { return parent_.size(); }

    uint32_t find(uint32_t x) {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);
        }
        return parent_[x];
    }

    /// Const overload — safe path compression via mutable parent_.
    /// No const_cast: the mutable keyword permits modification in const context.
    uint32_t find(uint32_t x) const {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);
        }
        return parent_[x];
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);
        if (ra == rb) {
            return;
        }
        if (rank_[ra] < rank_[rb]) {
            parent_[ra] = rb;
        } else if (rank_[ra] > rank_[rb]) {
            parent_[rb] = ra;
        } else {
            parent_[rb] = ra;
            rank_[ra]++;
        }
    }

    /// Extend the structure to hold `new_size` elements.
    /// New entries are initialized as disjoint singletons.
    void grow(size_t new_size) {
        for (size_t i = parent_.size(); i < new_size; ++i) {
            parent_.push_back(static_cast<uint32_t>(i));
            rank_.push_back(0);
        }
    }

private:
    mutable std::vector<uint32_t> parent_;
    std::vector<uint32_t> rank_;
};

} // namespace core::utils
