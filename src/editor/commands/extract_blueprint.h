#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include "editor/window/window_scope_id.h"
#include <optional>
#include <string>
#include <vector>

struct ComponentRegistry;

namespace editor::commands {

struct ExtractToBlueprintPreview {
    size_t selected_nodes = 0;
    size_t internal_wires = 0;
    size_t input_count = 0;
    size_t output_count = 0;
    size_t remapped_descendant_refs = 0;
    size_t passthrough_descendant_refs = 0;
    std::vector<std::string> input_iface_names;
    std::vector<std::string> output_iface_names;
    std::vector<std::string> iface_collision_names;
};

std::optional<ExtractToBlueprintPreview> build_extract_to_blueprint_preview(
    const bp2::Blueprint& source,
    const std::vector<core::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    core::StringInterner& interner,
    bp2::PathArena& arena,
    const ComponentRegistry& parser_registry,
    std::string* error_out = nullptr,
    bool allow_nonembedded_descendant_refs = false);

std::optional<bp2::Blueprint> build_extracted_blueprint_atomic(
    const bp2::Blueprint& source,
    const std::vector<core::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    core::StringInterner& interner,
    bp2::PathArena& arena,
    const ComponentRegistry& parser_registry,
    std::string* error_out = nullptr,
    bool allow_nonembedded_descendant_refs = false);

} // namespace editor::commands
