#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <cassert>
#include <string_view>
#include <unordered_map>
#include <string>
#include <vector>

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
// Paint / Hit / Interaction primitives
// ============================================================================

struct PaintCommand {
    ui::InternedId id;
    PaintPrimitiveKind kind = PaintPrimitiveKind::Text;
    std::string text;
    uint32_t fill_color = 0;
    uint32_t stroke_color = 0;
    float stroke_width = 0.0f;
    float inset = 0.0f;
    float text_size = 0.0f;
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
using ContentPresenterFn = PresentationNode (*)(const bp2::Blueprint::Node& node, ui::InternedId type_id);

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
};

struct NodePresentation {
    ui::InternedId node_id;
    NodeShellModel shell;
    PresentationNode content;
};

/// Compile a node presentation using a registered per-type presenter.
/// Requires a matching presenter in the registry; asserts on miss.
NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const bp2::Blueprint::Node& node,
                                           ui::InternedId type_id);

/// Compile a node presentation using render_hint-based frame classification
/// and the default content presenter for the node's content_type.
/// This is the primary entry point for the presentation compiler —
/// it works for ALL node kinds without requiring per-type registration.
NodePresentation compile_node_presentation(const bp2::Blueprint::Node& node);

// ============================================================================
// Default content presenter
// ============================================================================

/// Default content presenter that handles all bp2::NodeContentType variants.
/// Produces paint commands, hit regions, and interaction bindings for
/// Switch, VerticalToggle, Slider, Indicator, Knob, Gauge, Text, and None.
PresentationNode default_content_presenter(const bp2::Blueprint::Node& node, ui::InternedId type_id);

} // namespace editor::presentation
