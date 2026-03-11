#pragma once

/// v2 Blueprint schema — "Everything is a Blueprint"
///
/// Single unified format for:
/// - C++ component definitions (library)
/// - Composite blueprint definitions (library)
/// - Root editor documents (save files)
///
/// File extension: .blueprint

#include <string>
#include <vector>
#include <array>
#include <map>
#include <optional>

namespace an24::v2 {

// ==================================================================
// Primitives
// ==================================================================

using Pos = std::array<float, 2>;  // [x, y]

struct ExposedPort {
    std::string direction;  // "In", "Out", "InOut"
    std::string type;       // "V", "I", "Bool", "RPM", "Temperature", "Pressure", "Position", "Any"
    std::optional<std::string> alias;  // Signal merging alias (load-bearing for JIT/AOT)
};

struct ParamDef {
    std::string type;        // "float", "int", "bool", "string"
    std::string default_val;
};

// ==================================================================
// Node
// ==================================================================

struct ContentV2 {
    std::string kind;   // "gauge", "switch", "vertical_toggle", "hold_button", "text"
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
};

struct NodeColorV2 {
    float r = 0.5f, g = 0.5f, b = 0.5f, a = 1.0f;
};

struct NodeV2 {
    std::string type;                          // Component classname ("Battery", "Bus")
    Pos pos = {0.0f, 0.0f};
    Pos size = {0.0f, 0.0f};                  // {0,0} = use default from registry
    std::map<std::string, std::string> params;
    std::optional<ContentV2> content;
    std::optional<NodeColorV2> color;

    // Editor-only fields (root documents only, not library definitions)
    std::string display_name;                  // User-visible name (when different from key/id)
    std::string render_hint;                   // "bus", "ref", "group", "text", or empty
    bool expandable = false;                   // True for collapsed sub-blueprint nodes
    std::string group_id;                      // Sub-blueprint group membership (empty = top-level)
    std::string blueprint_path;                // Path to sub-blueprint definition
};

// ==================================================================
// Wire
// ==================================================================

struct WireEndV2 {
    std::string node;
    std::string port;
};

struct WireV2 {
    std::string id;
    WireEndV2 from;
    WireEndV2 to;
    std::vector<Pos> routing;
};

// ==================================================================
// Sub-blueprint
// ==================================================================

struct OverridesV2 {
    std::map<std::string, std::string> params;                 // "node.param" → value
    std::map<std::string, Pos> layout;                         // node_id → [x,y]
    std::map<std::string, std::vector<Pos>> routing;           // wire_id → routing points
};

struct SubBlueprintV2 {
    // Reference mode: template path present, no inline nodes
    std::optional<std::string> template_path;  // "library/systems/lamp_pass_through"

    // Type name for registry lookup (used by re-expansion on load)
    std::string type_name;

    // Visual properties
    Pos pos = {0.0f, 0.0f};
    Pos size = {0.0f, 0.0f};
    bool collapsed = true;

    // Overrides (reference mode only)
    std::optional<OverridesV2> overrides;

    // Embedded mode: nodes present (presence of nodes key → embedded)
    // template_path kept for provenance in embedded mode
    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;

    /// True if this sub-blueprint is embedded (has inline nodes)
    bool is_embedded() const { return !nodes.empty(); }
};

// ==================================================================
// Top-level Blueprint
// ==================================================================

struct MetaV2 {
    std::string name;
    std::string description;
    std::vector<std::string> domains;
    bool cpp_class = false;

    // Component metadata (preserved from TypeDefinition)
    std::string priority = "med";          // "high", "med", "low"
    bool critical = false;
    std::string content_type = "None";     // "None", "Gauge", "Switch", "Text", "VerticalToggle", "HoldButton"
    std::string render_hint;               // "bus", "ref", "group", "text", or empty
    bool visual_only = false;
    std::optional<Pos> size;               // Grid size {width, height}
};

struct ViewportV2 {
    Pos pan = {0.0f, 0.0f};
    float zoom = 1.0f;
    float grid = 16.0f;
};

struct BlueprintV2 {
    int version = 2;
    MetaV2 meta;

    std::map<std::string, ExposedPort> exposes;
    std::map<std::string, ParamDef> params;        // cpp_class=true components only

    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;
    std::map<std::string, SubBlueprintV2> sub_blueprints;

    std::optional<ViewportV2> viewport;            // Root documents only
};

// ==================================================================
// Parse / Serialize
// ==================================================================

/// Parse v2 JSON string → BlueprintV2. Returns nullopt on error or wrong version.
std::optional<BlueprintV2> parse_blueprint_v2(const std::string& json_text);

/// Serialize BlueprintV2 → pretty-printed JSON string.
std::string serialize_blueprint_v2(const BlueprintV2& bp);

} // namespace an24::v2
