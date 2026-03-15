#pragma once

/// Flat Blueprint schema — "Everything is a Blueprint"
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

// ==================================================================
// Primitives
// ==================================================================

using FlatPos = std::array<float, 2>;  // [x, y]

struct FlatPort {
    std::string direction;  // "In", "Out", "InOut"
    std::string type;       // "V", "I", "Bool", "RPM", "Temperature", "Pressure", "Position", "Any"
    std::optional<std::string> alias;  // Signal merging alias (load-bearing for JIT/AOT)
};

struct FlatParam {
    std::string type;        // "float", "int", "bool", "string"
    std::string default_val;
};

// ==================================================================
// Node
// ==================================================================

struct FlatContent {
    std::string kind;   // "gauge", "switch", "vertical_toggle", "hold_button", "text"
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
};

struct FlatColor {
    float r = 0.5f, g = 0.5f, b = 0.5f, a = 1.0f;
};

struct FlatNode {
    std::string type;                          // Component classname ("Battery", "Bus")
    FlatPos pos = {0.0f, 0.0f};
    FlatPos size = {0.0f, 0.0f};                  // {0,0} = use default from registry
    std::map<std::string, std::string> params;
    std::optional<FlatContent> content;
    std::optional<FlatColor> color;

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

struct FlatWireEnd {
    std::string node;
    std::string port;
};

struct FlatWire {
    std::string id;
    FlatWireEnd from;
    FlatWireEnd to;
    std::vector<FlatPos> routing;
};

// ==================================================================
// Sub-blueprint
// ==================================================================

struct FlatOverrides {
    std::map<std::string, std::string> params;                 // "node.param" → value
    std::map<std::string, FlatPos> layout;                         // node_id → [x,y]
    std::map<std::string, std::vector<FlatPos>> routing;           // wire_id → routing points
};

struct FlatSubBlueprint {
    // Reference mode: template path present, no inline nodes
    std::optional<std::string> template_path;  // "library/systems/lamp_pass_through"

    // Type name for registry lookup (used by re-expansion on load)
    std::string type_name;

    // Visual properties
    FlatPos pos = {0.0f, 0.0f};
    FlatPos size = {0.0f, 0.0f};
    bool collapsed = true;

    // Overrides (reference mode only)
    std::optional<FlatOverrides> overrides;

    // Embedded mode: nodes present (presence of nodes key → embedded)
    // template_path kept for provenance in embedded mode
    std::map<std::string, FlatNode> nodes;
    std::vector<FlatWire> wires;

    /// True if this sub-blueprint is embedded (has inline nodes)
    bool is_embedded() const { return !nodes.empty(); }
};

// ==================================================================
// Top-level Blueprint
// ==================================================================

struct FlatMeta {
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
    std::optional<FlatPos> size;               // Grid size {width, height}
};

struct FlatViewport {
    FlatPos pan = {0.0f, 0.0f};
    float zoom = 1.0f;
    float grid = 16.0f;
};

struct FlatBlueprint {
    int version = 2;
    FlatMeta meta;

    std::map<std::string, FlatPort> exposes;
    std::map<std::string, FlatParam> params;        // cpp_class=true components only

    std::map<std::string, FlatNode> nodes;
    std::vector<FlatWire> wires;
    std::map<std::string, FlatSubBlueprint> sub_blueprints;

    std::optional<FlatViewport> viewport;            // Root documents only
};

// ==================================================================
// Parse / Serialize
// ==================================================================

/// Parse v2 JSON string → FlatBlueprint. Returns nullopt on error or wrong version.
std::optional<FlatBlueprint> parse_flat_blueprint(const std::string& json_text);

/// Serialize FlatBlueprint → pretty-printed JSON string.
std::string serialize_flat_blueprint(const FlatBlueprint& bp);
