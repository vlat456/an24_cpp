#pragma once

#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include "editor/visual/node/bounds.h"
#include "editor/input/input_types.h"
#include "editor/visual/port/visual_port.h"
#include "blueprint_v2/blueprint/node_port.h"
#include "ui/core/interned_id.h"
#include "ui/math/pt.h"
#include "ui/math/rect.h"
#include <variant>
#include <vector>
#include <optional>

namespace visual {
    class Scene;

    struct HitEmpty {};
    struct HitContentInteraction {
        editor::presentation::InteractionKind kind = editor::presentation::InteractionKind::Click;
        float primary_min = 0.0f;
        float primary_max = 0.0f;
        int steps = 2;
    };

    struct HitNode {
        std::string_view node_id;
        ui::Pt world_pos{};
        ui::Pt size{};
        Bounds content_bounds{};
        editor::presentation::SemanticSceneSnapshot content_snapshot;
        bool renders_content_from_semantic_snapshot = false;
        std::optional<HitContentInteraction> content_interaction;
    };
    struct HitPort {
        std::string_view node_id;
        std::string_view port_name;
        bp2::Direction side = bp2::Direction::Input;
        PortType type = PortType::Any;
        ui::Pt center{};
    };
    struct HitWire {
        std::string_view wire_id;
        size_t segment = 0;
    };
    struct HitRoutingPoint {
        std::string_view wire_id;
        size_t index = 0;
        ui::Pt world_pos{};
    };
    struct HitResizeHandle {
        std::string_view node_id;
        ResizeCorner corner = ResizeCorner::BottomRight;
        ui::Pt world_pos{};
        ui::Pt size{};
    };

    using HitResult = std::variant<HitEmpty, HitNode, HitPort, HitWire, HitRoutingPoint, HitResizeHandle>;
}

namespace ui {
    class StringInterner;
}

namespace editor::presentation {

enum class CanvasRenderObjectKind {
    NodeBody,
    NodeFrame,
    NodeTitle,
    NodeFooter,
    ContentPaint,
    Port,
    Wire,
    RoutingPoint,
    ResizeHandle,
};

enum class CanvasHitObjectKind {
    NodeBody,
    ContentRegion,
    Port,
    WireSegment,
    RoutingPoint,
    ResizeHandle,
};

struct CanvasRenderObject {
    SceneObjectId id;
    ui::InternedId node_id;
    ui::InternedId element_id;
    CanvasRenderObjectKind kind = CanvasRenderObjectKind::ContentPaint;
    PrimitiveGeometry geometry;
    ui::Rect bounds;
    std::string text;
    uint32_t fill_color = 0;
    uint32_t stroke_color = 0;
    float stroke_width = 0.0f;
};

struct CanvasHitObject {
    SceneObjectId id;
    ui::InternedId node_id;
    ui::InternedId element_id;
    CanvasHitObjectKind kind = CanvasHitObjectKind::ContentRegion;
    HitShapeKind shape = HitShapeKind::Rectangle;
    ui::Rect bounds;

    // -- Port metadata (kind == Port) --
    bp2::Direction port_side = bp2::Direction::Input;
    PortType port_type = PortType::Any;

    // -- WireSegment metadata (kind == WireSegment) --
    size_t segment_index = 0;        ///< Index of the polyline segment within the wire
    ui::Pt segment_p0;               ///< Start point of this segment (for precise distance test)
    ui::Pt segment_p1;               ///< End point of this segment

    // -- RoutingPoint metadata (kind == RoutingPoint) --
    ui::InternedId rp_wire_id;       ///< Wire that owns this routing point
    size_t rp_index = 0;             ///< Index within the wire's routing point list

    // -- ResizeHandle metadata (kind == ResizeHandle) --
    ResizeCorner corner = ResizeCorner::BottomRight;
    ui::Pt node_world_pos;           ///< World position of the owning node
    ui::Pt node_size;                ///< Size of the owning node

    // -- NodeBody metadata (kind == NodeBody) --
    Bounds content_bounds{};
    SemanticSceneSnapshot content_snapshot;
    bool renders_content_from_semantic_snapshot = false;
    std::optional<InteractionBinding> content_interaction;
    bool is_group = false;  ///< GroupNodeWidget: border-only hit testing
};

struct CanvasSceneSnapshot {
    std::vector<CanvasRenderObject> render_objects;
    std::vector<CanvasHitObject> hit_objects;
};

/// Build a snapshot of the canvas scene by traversing the visual scene graph
/// and projecting widgets into explicit render and hit objects.
CanvasSceneSnapshot build_canvas_scene_snapshot(const visual::Scene& scene, ui::StringInterner& interner);

// ============================================================
// Snapshot-based hit testing (replaces visual::hit_test / hit_test_ports)
// ============================================================

/// Primary hit test against snapshot: returns highest-priority object under world_pos.
/// Priority: Port > RoutingPoint > ResizeHandle > Node (with content interaction) > WireSegment > Empty.
visual::HitResult hit_test_canvas_scene(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos,
                                        const ui::StringInterner& interner);

/// Port-only hit test against snapshot: returns HitPort if a port is under world_pos, else HitEmpty.
visual::HitResult hit_test_canvas_scene_ports(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos,
                                              const ui::StringInterner& interner);

} // namespace editor::presentation
