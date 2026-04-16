#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <cassert>
#include <string_view>
#include <unordered_map>
#include <string>
#include <variant>
#include <vector>
#include <cstdlib>

struct TypeDefinition;

namespace editor::presentation {

// ============================================================================
// Enums
// ============================================================================

enum class NodeFrameKind {
    Standard,
    Reference,
    Bus,
    Group,
    Annotation,
};

enum class LayoutKind {
    None,
    Row,
    Column,
    Overlay,
};

enum class PaintPrimitiveKind {
    Text,
    Rectangle,
    Circle,
    Arc,
    Line,
};

enum class HitShapeKind {
    Rectangle,
    Circle,
};

enum class InteractionKind {
    Click,
    Press,
    Release,
    DragScalar,
    DragDiscrete,
};

// ============================================================================
// Primitive geometry — explicit, type-safe, no overloaded fields
// ============================================================================

/// Text: positioned at (x, y) offset from element bounds origin.
/// If center_aligned is true, text is horizontally centered within element bounds.
struct TextGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float font_size = 10.0f;
    bool center_aligned = false;
};

/// Rectangle: axis-aligned box.
struct RectGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

/// Circle: center + radius.
struct CircleGeometry {
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
};

/// Line: radial line from center, defined by angle and inner/outer radii.
struct LineGeometry {
    float cx = 0.0f;
    float cy = 0.0f;
    float angle_deg = 0.0f;
    float inner_radius = 0.0f;
    float outer_radius = 0.0f;
};

/// Arc: circular arc from center, defined by radius, start angle, and sweep.
struct ArcGeometry {
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float start_angle_deg = 0.0f;
    float sweep_angle_deg = 0.0f;
};

using PrimitiveGeometry = std::variant<TextGeometry, RectGeometry, CircleGeometry, LineGeometry, ArcGeometry>;

struct GaugeMetrics {
    // Primary parameters — tune these
    float radius = 40.0f;
    float needle_length = 32.0f;
    float start_angle_deg = 210.0f;
    float sweep_angle_deg = -240.0f;
    float value_font_size = 14.0f;
    float unit_font_size = 10.0f;
    float center_dot_radius = 3.0f;
    float major_tick_inset = 6.0f;
    float minor_tick_inset = 3.0f;
    float value_text_gap = 5.0f;   ///< gap between arc bottom and value text top
    float unit_text_gap = 2.0f;    ///< gap between value text bottom and unit text top

    // Derived — all geometry flows from the primaries above
    constexpr float diameter() const { return radius * 2.0f; }
    constexpr float major_tick_inner_radius() const { return radius - major_tick_inset; }
    constexpr float minor_tick_inner_radius() const { return radius - minor_tick_inset; }
    constexpr float value_text_y() const { return diameter() + value_text_gap; }
    constexpr float unit_text_y() const { return value_text_y() + value_font_size + unit_text_gap; }
    constexpr float preferred_width() const { return diameter(); }
    constexpr float preferred_height() const { return unit_text_y() + unit_font_size; }
    constexpr float center_offset_y() const { return radius - preferred_height() * 0.5f; }
};

constexpr GaugeMetrics gauge_metrics() {
    return GaugeMetrics{};
}

// ============================================================================
// Presentation spec — sole input to the presentation compiler
// ============================================================================

/// Self-contained, resolved input for the presentation compiler.
/// Replaces bp2::Blueprint::Node as the compiler's parameter — no widget-era
/// hydrated view data leaks into the compiler contract.
struct PresentationSpec {
    // Identity
    ui::InternedId node_id;
    ui::InternedId type_id;

    // Shell
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    std::string title;

    // Content
    bp2::NodeContentType content_type = bp2::NodeContentType::None;
    std::string content_label;
    float content_min = 0.0f;
    float content_max = 1.0f;
    float content_value = 0.0f;
    std::string content_unit;
    bool content_state = false;
    bool content_tripped = false;

    // Annotation (only meaningful when frame_kind == Annotation)
    std::string annotation_text;
    float annotation_font_size = 12.0f;
};

/// Resolve NodeFrameKind from TypeDefinition (canonical authority).
/// Falls back to NodeFrameKind::Standard if def is null or render_hint is empty.
NodeFrameKind resolve_frame_kind(const struct TypeDefinition* def);

/// Build a PresentationSpec from TypeDefinition + semantic data (canonical path).
/// This is the sole authority — reads from the source of truth, not from
/// hydrated view mirrors.
PresentationSpec make_presentation_spec(const bp2::Blueprint::Node& node,
                                        const struct TypeDefinition* def,
                                        ui::StringInterner& interner);

// ============================================================================
// Paint / Hit / Interaction primitives
// ============================================================================

struct PaintCommand {
    ui::InternedId id;
    PaintPrimitiveKind kind = PaintPrimitiveKind::Text;
    PrimitiveGeometry geometry;
    std::string text;
    uint32_t fill_color = 0;
    uint32_t stroke_color = 0;
    float stroke_width = 0.0f;
};

struct HitRegion {
    ui::InternedId id;
    HitShapeKind kind = HitShapeKind::Rectangle;
};

struct InteractionBinding {
    ui::InternedId region_id;
    InteractionKind kind = InteractionKind::Click;
    ui::InternedId action_id;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float step = 0.0f;
};

// ============================================================================
// Presentation tree
// ============================================================================

struct PresentationNode {
    ui::InternedId element_id;
    LayoutKind layout = LayoutKind::None;
    float gap = 0.0f;
    std::vector<PaintCommand> paint;
    std::vector<HitRegion> hit_regions;
    std::vector<InteractionBinding> interactions;
    std::vector<PresentationNode> children;
};

// ============================================================================
// Shell model
// ============================================================================

struct NodeShellModel {
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    std::string title;
    std::string type_name;          ///< Footer type label (empty for non-standard frames)
    std::string annotation_text;    ///< Body text for Annotation frames
    float annotation_font_size = 12.0f;
};

// ============================================================================
// Frame kind classification (replaces widget-type dynamic_cast)
// ============================================================================

/// Classify render_hint string → NodeFrameKind.
/// This is the single authority for node visual classification.
NodeFrameKind classify_frame_kind(std::string_view render_hint);

// ============================================================================
// Content presenter registry
// ============================================================================

/// Content presenters remain stateless function pointers for now. Widen this
/// contract before broader adoption if presenters need injected dependencies.
using ContentPresenterFn = PresentationNode (*)(const PresentationSpec& spec);

struct NodePresenter {
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    ContentPresenterFn content = nullptr;
};

class NodePresenterRegistry {
public:
    void register_presenter(ui::InternedId type_id, NodePresenter presenter);
    const NodePresenter* find_presenter(ui::InternedId type_id) const;

private:
    std::unordered_map<ui::InternedId, NodePresenter> presenters_;
};

// ============================================================================
// Compile context and output
// ============================================================================

struct NodePresentationCompileContext {
    const NodePresenterRegistry* registry = nullptr;
    std::string_view (*resolve_type_name)(ui::InternedId type_id, void* user_data) = nullptr;
    void* resolve_type_name_user_data = nullptr;
};

struct NodePresentation {
    ui::InternedId node_id;
    NodeShellModel shell;
    PresentationNode content;
};

/// Compile a node presentation from a PresentationSpec.
/// If a registry is provided in ctx and contains a presenter for spec.type_id,
/// that presenter is used. Otherwise falls back to the default content presenter.
NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const PresentationSpec& spec);

/// Convenience overload for tests and compiler-only call sites that do not need
/// type label resolution or a registry.
NodePresentation compile_node_presentation(const PresentationSpec& spec);

// ============================================================================
// Default content presenter
// ============================================================================

/// Default content presenter that handles all bp2::NodeContentType variants.
/// Produces paint commands, hit regions, and interaction bindings for
/// Switch, VerticalToggle, Slider, Indicator, Knob, Gauge, Text, and None.
PresentationNode default_content_presenter(const PresentationSpec& spec);

} // namespace editor::presentation
