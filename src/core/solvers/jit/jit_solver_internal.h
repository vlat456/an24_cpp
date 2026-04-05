#pragma once

#include "jit_solver.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Internal implementation details - not exposed to external users
namespace jit_solver_impl {

/// Helper functions and utilities for build_systems_dev
std::string metadata_classname_for(std::string_view classname);
bool is_knob_switch_family(std::string_view classname);
bool is_scheduler_source_component_class(std::string_view classname);
bool is_solver_owned_electrical_propagator(std::string_view classname);
std::vector<std::string> active_source_writer_ports_for(std::string_view classname);
std::unordered_set<std::string> output_ports_for_class(std::string_view classname);
bool parse_bool_param_value(const std::string& value);

/// ParamReader class for parameter parsing
class ParamReader {
public:
    ParamReader(const std::unordered_map<std::string, std::string>& params, const DeviceInstance& dev)
        : params_(params), dev_(dev) {}

    float consume_float_optional(const std::string& key, float default_val);
    bool consume_bool_optional(const std::string& key, bool default_val);
    std::string consume_string_optional(const std::string& key, const std::string& default_val);
    float consume_float_required(const std::string& key);
    bool consume_bool_required(const std::string& key);
    void validate_all_consumed() const;

private:
    const std::string& get_required(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& params_;
    const DeviceInstance& dev_;
    std::unordered_set<std::string> consumed_params_;
};

/// Signal mapping and port union functions
void process_port_unions(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections);

/// Component factory functions
void build_and_register_components(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices);

bool try_build_logic_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader);

bool try_build_control_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader);

bool try_build_utility_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader);

bool try_build_physical_component(
    BuildResult& result,
    const DeviceInstance& dev,
    ParamReader& param_reader);

void validate_source_writer_conflicts(
    const BuildResult& result,
    const std::vector<DeviceInstance>& devices);

void validate_consumer_guardrails(
    const BuildResult& result,
    const std::vector<std::string>& consumer_device_names,
    const std::vector<DeviceInstance>& devices);

void topological_sort_consumers(
    BuildResult& result,
    std::vector<std::string>& consumer_device_names,
    const std::vector<DeviceInstance>& devices);

/// Electrical building functions
void build_electrical_islands(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices);

void build_electrical_patch_ops(BuildResult& result);

void populate_solver_owned_refs(BuildResult& result);

}  // namespace jit_solver_impl
