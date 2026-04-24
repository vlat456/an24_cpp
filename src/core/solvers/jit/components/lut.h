#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"
#include <cstdint>
#include <string>
#include <vector>

/// LUT - Lookup table with linear interpolation.
/// Table data lives in SimulationState arena (cache-friendly, contiguous).
/// Component holds only offset+size into the shared arena.
template <typename Provider = JitProvider>
class LUT {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    uint32_t table_offset = 0;  ///< Index into st.lut_keys / st.lut_values
    uint16_t table_size   = 0;  ///< Number of breakpoints

    LUT() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}

    /// Parse "k1:v1; k2:v2; ..." table string into keys/values vectors
    static bool parse_table(const std::string& table_str,
                            std::vector<float>& keys,
                            std::vector<float>& values);

    static float interpolate(float x, const float* keys, const float* vals, uint16_t size);
};
