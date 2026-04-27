#pragma once

#include "core/model/component_types.h"
#include "core/model/connection.h"
#include "core/model/device_instance.h"

#include <nlohmann/json.hpp>

namespace json_io_detail {

DeviceInstance parse_device(const nlohmann::json& j);
Connection parse_connection(const nlohmann::json& j);

/// Parse a patch_op JSON object into PatchOpDecl.
/// Returns std::nullopt if the JSON is not a valid patch_op object.
/// Used by both component_registry_json_loader and type_definition_json.
std::optional<PatchOpDecl> parse_patch_op(const nlohmann::json& po);

} // namespace json_io_detail
