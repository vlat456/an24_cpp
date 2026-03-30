#include "lut.h"
#include "port_registry.h"
#include "../../parse_number.h"
#include <algorithm>

template <typename Provider>
void LUT<Provider>::execute(SimulationState& st, float /*dt*/) {
    float x = st.values[provider.get(PortNames::input)];
    const float* keys = st.lut_keys.data() + table_offset;
    const float* vals = st.lut_values.data() + table_offset;
    st.values[provider.get(PortNames::output)] = interpolate(x, keys, vals, table_size);
}

template <typename Provider>
void LUT<Provider>::commit(SimulationState& st) {
    (void)st;
}

template <typename Provider>
bool LUT<Provider>::parse_table(const std::string& table_str,
                                std::vector<float>& keys,
                                std::vector<float>& values) {
    keys.clear();
    values.clear();
    if (table_str.empty()) return false;

    size_t pos = 0;
    while (pos < table_str.size()) {
        // Skip whitespace and semicolons
        while (pos < table_str.size() && (table_str[pos] == ' ' || table_str[pos] == ';'))
            ++pos;
        if (pos >= table_str.size()) break;

        // Find colon separator
        size_t colon = table_str.find(':', pos);
        if (colon == std::string::npos) break;

        // Find end of value (next semicolon or end)
        size_t end = table_str.find(';', colon + 1);
        if (end == std::string::npos) end = table_str.size();

        {
            // Locale-independent parsing with whitespace tolerance
            std::string k_str = table_str.substr(pos, colon - pos);
            std::string v_str = table_str.substr(colon + 1, end - colon - 1);
            float kf, vf;
            if (!locale_safe::parse_float(k_str, kf) ||
                !locale_safe::parse_float(v_str, vf)) break;
            keys.push_back(kf);
            values.push_back(vf);
        }
        pos = end;
    }
    return !keys.empty();
}

template <typename Provider>
float LUT<Provider>::interpolate(float x, const float* keys, const float* vals, uint16_t size) {
    if (size == 0) return 0.0f;
    if (size == 1) return vals[0];
    
    // Clamp to bounds
    if (x <= keys[0]) return vals[0];
    if (x >= keys[size - 1]) return vals[size - 1];
    
    // Find the interval
    for (uint16_t i = 0; i < size - 1; ++i) {
        if (x >= keys[i] && x <= keys[i + 1]) {
            float span = keys[i + 1] - keys[i];
            if (span < 1e-9f) return vals[i]; // Guard: duplicate breakpoints
            float t = (x - keys[i]) / span;
            return vals[i] + t * (vals[i + 1] - vals[i]);
        }
    }
    
    return vals[size - 1];  // Fallback
}

template class LUT<JitProvider>;
