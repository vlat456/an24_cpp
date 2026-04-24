#pragma once

#include <string>

namespace codegen_detail {

/// Sanitize a string for use as a C++ identifier in generated code.
/// Replaces characters that are illegal in identifiers.
inline std::string sanitize_name(const std::string& s) {
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

} // namespace codegen_detail
