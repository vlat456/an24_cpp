/// Locale-independent numeric parsing and formatting utilities.
///
/// std::stof / std::stod / operator<< are locale-sensitive: on systems
/// with a European locale, ',' becomes the decimal separator, breaking
/// all serialized simulation data.  These helpers always use '.' as the
/// decimal separator regardless of the active locale.
///
/// std::from_chars(float) is unavailable on some toolchains (macOS libc++),
/// so we validate the structure manually and then delegate to strtod_l
/// (POSIX) with a C locale, guaranteeing '.' as the decimal separator
/// even if setlocale() has been called (e.g. by SDL or GTK).

#pragma once

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <xlocale.h>  // strtod_l, newlocale on macOS; also works on glibc
#endif

namespace locale_safe {

namespace detail {

/// Thread-safe, lazily-initialized C locale for strtod_l / snprintf_l.
/// The locale object is created once and never freed (intentional leak
/// at program exit — harmless, avoids static-destruction-order issues).
inline locale_t c_locale() {
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    static locale_t loc = newlocale(LC_ALL_MASK, "C", nullptr);
    return loc;
#else
    return nullptr;  // fallback: strtod used (see parse_float)
#endif
}

} // namespace detail

/// Parse a float from a string using '.' as the decimal separator.
/// Returns true on success and writes the result to `out`.
/// Rejects strings with trailing garbage or locale-specific characters.
inline bool parse_float(const std::string& s, float& out) {
    if (s.empty()) return false;
    const char* p   = s.data();
    const char* end = p + s.size();

    // Skip leading whitespace
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p == end) return false;

    // Optional sign
    if (*p == '+' || *p == '-') ++p;
    if (p == end) return false;

    bool has_digit = false;

    // Integer part
    while (p < end && *p >= '0' && *p <= '9') { ++p; has_digit = true; }

    // Decimal part
    if (p < end && *p == '.') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') { ++p; has_digit = true; }
    }

    // Optional exponent
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) ++p;
        if (p == end || *p < '0' || *p > '9') return false;
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }

    // Skip trailing whitespace
    while (p < end && (*p == ' ' || *p == '\t')) ++p;

    if (!has_digit || p != end) return false;

    // Structural validation passed — delegate to strtod_l (locale-safe)
    // or strtod (fallback) for the actual numeric conversion.
    char* strtod_end = nullptr;
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    double d = strtod_l(s.c_str(), &strtod_end, detail::c_locale());
#else
    double d = std::strtod(s.c_str(), &strtod_end);
#endif
    if (strtod_end == s.c_str()) return false;  // strtod consumed nothing
    out = static_cast<float>(d);
    return true;
}

/// Parse a float from a string, returning `default_val` on failure.
inline float parse_float_or(const std::string& s, float default_val) {
    float out;
    return parse_float(s, out) ? out : default_val;
}

/// Parse a 64-bit integer using std::from_chars (always locale-independent).
inline bool parse_int64(const std::string& s, long long& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

/// Check if a string is a valid float literal (structural validation only).
inline bool is_float_literal(const std::string& s) {
    float dummy;
    return parse_float(s, dummy);
}

/// Check if a string is a valid integer literal (optional sign + digits).
inline bool is_int_literal(const std::string& s) {
    if (s.empty()) return false;
    const char* p   = s.data();
    const char* end = p + s.size();
    if (*p == '+' || *p == '-') ++p;
    if (p == end) return false;
    while (p < end && *p >= '0' && *p <= '9') ++p;
    return p == end;
}

/// Format a float to a string using '.' as the decimal separator.
/// Always produces at least one decimal digit (e.g. "1.0", not "1").
inline std::string format_float(float v, const char* fmt = "%g") {
    char buf[64];
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    snprintf_l(buf, sizeof(buf), detail::c_locale(), fmt, static_cast<double>(v));
#else
    std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(v));
#endif
    std::string result(buf);
    // Ensure it looks like a float literal
    if (result.find('.') == std::string::npos &&
        result.find('e') == std::string::npos &&
        result.find('E') == std::string::npos) {
        result += ".0";
    }
    return result;
}

} // namespace locale_safe
