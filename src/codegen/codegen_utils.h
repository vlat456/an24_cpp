#pragma once

#include <string>

inline std::string sanitize_codegen_name(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '.': result += "_DOT_"; break;
            case '-': result += "_DASH_"; break;
            case ':': result += "_"; break;
            default:  result += c; break;
        }
    }
    return result;
}
