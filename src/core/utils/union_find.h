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

    uint32_t find(uint32_t x) {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);
        }
        return parent_[x];
    }

    uint32_t find(uint32_t x) const {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
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

private:
    mutable std::vector<uint32_t> parent_;
    std::vector<uint32_t> rank_;
};

} // namespace core::utils
