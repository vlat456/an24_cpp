#include "jit_solver_internal.h"
#include "../../../parse_number.h"
#include <spdlog/spdlog.h>

namespace jit_solver_impl {

std::string metadata_classname_for(std::string_view classname) {
    return std::string(classname);
}

bool is_knob_switch_family(std::string_view classname) {
    return classname == "KnobSwitch" ||
           classname == "RotarySwitch1ToN" ||
           classname == "RotarySwitchNTo1";
}

std::vector<std::string> active_source_writer_ports_for(std::string_view classname) {
    return get_source_writer_ports(
        metadata_classname_for(classname),
        static_cast<uint8_t>(Domain::Electrical));
}

std::unordered_set<std::string> output_ports_for_class(std::string_view classname) {
    auto outputs = get_output_ports(metadata_classname_for(classname));
    return std::unordered_set<std::string>(outputs.begin(), outputs.end());
}

bool parse_bool_param_value(const std::string& value) {
    return value == "true" || value == "1";
}

// === ParamReader Implementation ===

float ParamReader::consume_float_optional(const std::string& key, float default_val) {
    consumed_params_.insert(key);
    auto it = params_.find(key);
    if (it != params_.end()) {
        return locale_safe::parse_float_or(it->second, default_val);
    }
    return default_val;
}

bool ParamReader::consume_bool_optional(const std::string& key, bool default_val) {
    consumed_params_.insert(key);
    auto it = params_.find(key);
    if (it != params_.end()) {
        return parse_bool_param_value(it->second);
    }
    return default_val;
}

std::string ParamReader::consume_string_optional(const std::string& key, const std::string& default_val) {
    consumed_params_.insert(key);
    auto it = params_.find(key);
    if (it != params_.end()) {
        return it->second;
    }
    return default_val;
}

float ParamReader::consume_float_required(const std::string& key) {
    consumed_params_.insert(key);
    return locale_safe::parse_float_or(get_required(key), 0.0f);
}

bool ParamReader::consume_bool_required(const std::string& key) {
    consumed_params_.insert(key);
    return parse_bool_param_value(get_required(key));
}

void ParamReader::validate_all_consumed() const {
    for (const auto& [key, val] : params_) {
        (void)val;
        if (consumed_params_.find(key) == consumed_params_.end()) {
            throw std::runtime_error("Unknown/unconsumed parameter '" + key +
                "' for component '" + dev_.name + "' (classname: " + dev_.classname + ")");
        }
    }
}

const std::string& ParamReader::get_required(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end()) {
        std::string available;
        for (const auto& [k, v] : params_) {
            (void)v;
            if (!available.empty()) {
                available += ", ";
            }
            available += k;
        }
        throw std::runtime_error("Missing required parameter '" + key +
            "' for component '" + dev_.name + "' (classname: " + dev_.classname +
            "). Available keys: " + available);
    }
    return it->second;
}

}  // namespace
