#pragma once

#include "blueprint_codec.h"
#include "core/model/component_registry.h"

#include <nlohmann/json.hpp>
#include <initializer_list>
#include <unordered_set>
#include <optional>

namespace bp2::codec_detail {

// ============================================================
// Parsing helpers (common)
// ============================================================

float parse_finite_float(nlohmann::json const& value, std::string const& field_name);
bool parse_number_string(std::string const& s, float& out);
bool parse_bool_string(std::string const& s, std::string& normalized);
bool parse_vec2_string(std::string const& s);

std::string float_to_string(float v);
std::string port_type_to_string(PortType t);
std::string domain_to_string(Domain d);

void assign_param_by_descriptor(Blueprint::Node& node,
                                core::StringInterner& interner,
                                std::string const& key,
                                nlohmann::json const& val,
                                ParamSpec const& schema,
                                ComponentSpec const* type_def);

bool is_known_port_type_value(int v);

// ============================================================
// Port type conversion (unified, used by both encode and decode)
// ============================================================

/// Parse a port type string name (e.g. "V", "Bool") to PortType.
/// Returns std::nullopt if the name is not recognized.
std::optional<PortType> port_type_from_name(std::string_view s);

// ============================================================
// Decode field helpers — consolidate repeated patterns
// ============================================================

/// Validate that all keys in a JSON object are in the allowed set.
/// Throws std::runtime_error with "unknown <context> field: <key>" on violation.
void check_allowed_fields(nlohmann::json const& obj,
                          std::unordered_set<std::string> const& allowed,
                          std::string const& context);

/// Require a field to exist and have a specific JSON type.
/// `context` is used as prefix in error messages (e.g. "invalid node entry").
/// `type_name` is the human-readable type (e.g. "string", "integer").
/// Throws std::runtime_error on failure.
void require_field(nlohmann::json const& obj,
                   std::string const& field,
                   bool (nlohmann::json::*type_check)() const,
                   std::string const& context,
                   std::string const& type_name);

/// Read an optional string field from a JSON object.
/// Throws if the field exists but is not a string.
std::optional<std::string> read_optional_string(nlohmann::json const& obj,
                                                 std::string const& field,
                                                 std::string const& context);

/// Read an optional bool field from a JSON object.
/// Throws if the field exists but is not a boolean.
std::optional<bool> read_optional_bool(nlohmann::json const& obj,
                                        std::string const& field,
                                        std::string const& context);

/// Read an optional numeric field as finite float from a JSON object.
/// Throws if the field exists but is not numeric or is non-finite.
std::optional<float> read_optional_float(nlohmann::json const& obj,
                                          std::string const& field,
                                          std::string const& context);

nlohmann::json encode_interface(Interface const& iface,
                                core::StringInterner const& interner,
                                ComponentSpec const* type_def);

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                            core::StringInterner const& interner,
                            PathArena const& arena,
                            ::ComponentRegistry const* parser_registry);

nlohmann::json encode_wires(std::vector<Blueprint::Wire> const& wires,
                            core::StringInterner const& interner);

Interface decode_interface(nlohmann::json const& arr,
                           core::StringInterner& interner);

Blueprint decode_nodes(Blueprint bp,
                       nlohmann::json const& arr,
                       core::StringInterner& interner,
                       ::ComponentRegistry const& parser_registry);

Blueprint decode_wires(Blueprint bp,
                       nlohmann::json const& arr,
                       core::StringInterner& interner);

/// Resolve wire domain fields from endpoint port types after decode.
/// v1 format does not persist wire domain; this pass infers it from
/// resolved endpoint ports so the invariant checker can verify it.
Blueprint resolve_wire_domains(Blueprint bp,
                               ::ComponentRegistry const& parser_registry,
                               core::StringInterner& interner);

} // namespace bp2::codec_detail
