#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace editor {

std::optional<std::string_view> select_slider_readback_port(const bp2::Blueprint::Node& node,
                                                             ui::StringInterner& interner);

/// Recursively walk all nodes in a blueprint (including embedded blueprints),
/// building a typed instance_path as the recursion descends. The callback
/// receives each node and its current instance_path.
void walk_blueprint_nodes(
    const bp2::Blueprint& bp,
    std::vector<ui::InternedId>& instance_path,
    const std::function<void(const bp2::Blueprint::Node&, std::span<const ui::InternedId>)>& fn);

/// Convert a typed instance path to a colon-delimited scope string for
/// simulation signal key construction. Returns "" for root scope (empty path).
inline std::string instance_path_to_scope_string(
    const ui::StringInterner& interner,
    std::span<const ui::InternedId> path) {
    if (path.empty()) return "";
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) result += ':';
        result += interner.resolve(path[i]);
    }
    return result;
}

} // namespace editor
