#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "../json_parser/json_parser.h"

namespace an24 {

/// Convert domain string to enum
inline Domain parse_domain(const std::string& domain_str) {
    if (domain_str.find("Electrical") != std::string::npos) return Domain::Electrical;
    if (domain_str.find("Logical") != std::string::npos) return Domain::Logical;
    if (domain_str.find("Mechanical") != std::string::npos) return Domain::Mechanical;
    if (domain_str.find("Hydraulic") != std::string::npos) return Domain::Hydraulic;
    if (domain_str.find("Thermal") != std::string::npos) return Domain::Thermal;
    return Domain::Electrical; // default
}

/// Check if component should run on given step for its domain
inline bool should_run_on_step(Domain domain, int step) {
    if (has_domain(domain, Domain::Electrical) || has_domain(domain, Domain::Logical)) {
        return true;  // Every step (60 Hz)
    }
    if (has_domain(domain, Domain::Mechanical)) {
        return (step % 3) == 0;  // Every 3rd step (20 Hz)
    }
    if (has_domain(domain, Domain::Hydraulic)) {
        return (step % 12) == 0;  // Every 12th step (5 Hz)
    }
    if (has_domain(domain, Domain::Thermal)) {
        return (step % 60) == 0;  // Every 60th step (1 Hz)
    }
    return true;
}

/// Get step frequency for domain (in Hz) - returns highest frequency for multi-domain
inline int get_domain_frequency(Domain domain) {
    if (has_domain(domain, Domain::Electrical) || has_domain(domain, Domain::Logical)) {
        return 60;
    }
    if (has_domain(domain, Domain::Mechanical)) {
        return 20;
    }
    if (has_domain(domain, Domain::Hydraulic)) {
        return 5;
    }
    if (has_domain(domain, Domain::Thermal)) {
        return 1;
    }
    return 60;
}

/// Get domain name string (for single domain)
inline const char* get_domain_name(Domain domain) {
    switch (domain) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Thermal: return "Thermal";
        default: return "Mixed";
    }
}

/// Get domain mask as string (for multi-domain)
inline std::string get_domain_mask_string(Domain mask) {
    std::string result;
    if (has_domain(mask, Domain::Electrical)) result += "Electrical ";
    if (has_domain(mask, Domain::Logical)) result += "Logical ";
    if (has_domain(mask, Domain::Mechanical)) result += "Mechanical ";
    if (has_domain(mask, Domain::Hydraulic)) result += "Hydraulic ";
    if (has_domain(mask, Domain::Thermal)) result += "Thermal ";
    if (result.empty()) result = "None";
    return result;
}

} // namespace an24
