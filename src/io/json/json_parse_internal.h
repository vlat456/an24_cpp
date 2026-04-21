#pragma once

#include "core/model/connection.h"
#include "core/model/device_instance.h"

#include <nlohmann/json.hpp>

namespace json_io_detail {

DeviceInstance parse_device(const nlohmann::json& j);
Connection parse_connection(const nlohmann::json& j);

} // namespace json_io_detail
