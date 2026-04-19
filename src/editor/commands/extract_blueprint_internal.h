#pragma once

#include "extract_blueprint.h"

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include "editor/window/window_scope_id.h"

#include "editor/common/port_type_utils.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor::commands::extract_detail {

inline constexpr float kBridgeMarginX      = 160.0f;
inline constexpr float kDefaultNodeWidth   = 100.0f;
inline constexpr float kDefaultNodeHeight  = 64.0f;
inline constexpr float kFallbackLaneStartY = 40.0f;
inline constexpr float kFallbackLaneStepY  = 80.0f;
inline constexpr float kMultiLaneOffsetY   = 16.0f;

struct ExternalConnection {
    bool is_input = false;
    ui::InternedId external_node_id;
    ui::InternedId external_port;
    ui::InternedId internal_node_id;
    ui::InternedId internal_port;
    std::string iface_name;
    Domain domain = Domain::Electrical;
    PortType port_type = PortType::Any;
    ui::InternedId original_wire_id;
};

struct ExtractionPlan {
    std::vector<bp2::Blueprint::Node> internal_nodes;
    std::vector<bp2::Blueprint::Wire> internal_wires;
    std::vector<ExternalConnection> inputs;
    std::vector<ExternalConnection> outputs;
    std::unordered_set<ui::InternedId> selected_set;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
};

struct DescendantRemapStats {
    size_t remapped = 0;
    size_t passthrough = 0;
};

// ========================================================================
// Inline helpers — small, used across multiple TUs
// ========================================================================

/// Resolve PortType: use the stored port_type if concrete, otherwise
/// fall back to the canonical port type for the wire's domain.
inline PortType resolve_port_type(const ExternalConnection& ec) {
    return (ec.port_type == PortType::Any)
        ? editor::common::port_type_for_domain(ec.domain)
        : ec.port_type;
}

/// Set *error_out (when non-null) and return false — reduces repetitive
/// three-line fail blocks to a single expression.
inline bool set_error(std::string* error_out, const char* msg) {
    if (error_out) *error_out = msg;
    return false;
}

// ========================================================================
// Function declarations
// ========================================================================

PortType find_port_type(const bp2::Blueprint& bp,
                        const bp2::Blueprint::Node* node,
                        ui::InternedId port_name,
                        const ComponentRegistry& registry,
                        ui::StringInterner& interner);

bool path_to_node_port(const bp2::Path& path,
                       const bp2::PathArena& arena,
                       ui::InternedId& out_node,
                       ui::InternedId& out_port);

bool path_to_node_port(const bp2::WireEndpoint& ep,
                       const bp2::PathArena& arena,
                       ui::InternedId& out_node,
                       ui::InternedId& out_port);

std::string dedupe_name(const std::string& base,
                        std::unordered_set<std::string>& used);

ui::InternedId next_unique_id(ui::StringInterner& interner,
                              const std::unordered_set<ui::InternedId>& used,
                              const std::string& prefix);

std::unordered_set<ui::InternedId> collect_used_node_ids(const bp2::Blueprint& bp);

std::unordered_set<ui::InternedId> collect_used_wire_ids(const bp2::Blueprint& bp);

bool compare_external(const ExternalConnection& a, const ExternalConnection& b);

std::unordered_map<ui::InternedId, float> build_node_center_y_map(
    const std::vector<bp2::Blueprint::Node>& nodes);

float fallback_lane_y(size_t index);

ui::InternedId make_iface_bridge_id(ui::StringInterner& interner,
                                    ui::InternedId nested_instance_id,
                                    const std::string& iface_name);

/// Dedupe iface names for a list of external connections.
/// Assigns a default name if empty, then deduplicates using dedupe_name.
void dedupe_iface_names(std::vector<ExternalConnection>& conns,
                        const char* default_name);

struct BridgeSideBuildParams {
    const std::vector<ExternalConnection>& conns;
    bool is_input_side = true;
    const std::unordered_map<ui::InternedId, float>& node_center_y;
    float x = 0.0f;
    float fallback_y_origin = 0.0f;
    WindowScopeId scope_id;
    const char* unique_prefix = "";
    const ui::InternedId* canonical_nested_instance_id = nullptr;
};

bool create_bridge_nodes_for_side(
    bp2::Blueprint& out,
    const BridgeSideBuildParams& p,
    ui::StringInterner& interner,
    std::unordered_set<ui::InternedId>& used_node_ids,
    std::unordered_map<std::string, ui::InternedId>& out_bridge_ids,
    std::string* error_out);

void append_bridge_to_internal_wires(
    bp2::Blueprint& out,
    const std::vector<ExternalConnection>& conns,
    bool is_input_side,
    const std::unordered_map<std::string, ui::InternedId>& bridge_ids,
    const char* wire_prefix,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::unordered_set<ui::InternedId>& used_wire_ids);

bool validate_blueprint_name_for_extract(const bp2::Blueprint& source,
                                         const std::string& blueprint_name,
                                         ui::StringInterner& interner,
                                         ui::InternedId* blueprint_iid_out,
                                         std::string* error_out);

std::optional<ExtractionPlan> analyze_selection(const bp2::Blueprint& bp,
                                                 const std::vector<ui::InternedId>& selected_ids,
                                                 const WindowScopeId& scope_id,
                                                  bool allow_nonembedded_descendant_refs,
                                                  ui::StringInterner& interner,
                                                  const bp2::PathArena& arena,
                                                  const ComponentRegistry& registry,
                                                  std::string* error_out);

DescendantRemapStats collect_descendant_remap_stats(
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs);

std::optional<bp2::Blueprint> build_parent_blueprint_from_plan(
    const bp2::Blueprint& source,
    const ExtractionPlan& plan,
    ui::InternedId blueprint_iid,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    bool allow_nonembedded_descendant_refs,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out);

} // namespace editor::commands::extract_detail
