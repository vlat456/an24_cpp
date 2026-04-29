#pragma once

#include "core/strings/interned_id.h"
#include <functional>

/// Callback invoked when a modal window applies changes.
/// Receives the InternedId of the target node.
using WindowNodeCallback = std::function<void(core::InternedId node_id)>;
