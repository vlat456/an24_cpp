#pragma once

#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"
#include <vector>

namespace editor::presentation {

class SceneObjectId {
public:
    constexpr SceneObjectId() noexcept : value_(0) {}
    constexpr explicit SceneObjectId(uint32_t value) noexcept : value_(value) {}

    constexpr bool empty() const noexcept { return value_ == 0; }
    constexpr uint32_t raw() const noexcept { return value_; }

    constexpr bool operator==(SceneObjectId other) const noexcept { return value_ == other.value_; }
    constexpr bool operator!=(SceneObjectId other) const noexcept { return value_ != other.value_; }

private:
    uint32_t value_;
};

enum class SceneRenderObjectKind {
    NodeFrame,
    NodeTitle,
    ContentPaint,
};

/// Render layer derived from kind. Lower numeric value = painted first (back-to-front).
constexpr int render_layer_order(SceneRenderObjectKind kind) {
    switch (kind) {
        case SceneRenderObjectKind::NodeFrame:    return 0;
        case SceneRenderObjectKind::NodeTitle:    return 1;
        case SceneRenderObjectKind::ContentPaint: return 2;
    }
    return 0;
}

struct SceneRenderObject {
    SceneObjectId id;
    ui::InternedId node_id;
    ui::InternedId element_id;
    SceneRenderObjectKind kind = SceneRenderObjectKind::ContentPaint;
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    PaintPrimitiveKind primitive = PaintPrimitiveKind::Rectangle;
    Rect bounds;
    std::string text;
};

enum class SceneHitObjectKind {
    NodeBody,
    ContentRegion,
};

/// Hit layer derived from kind. Higher numeric value = tested first (front-to-back).
constexpr int hit_layer_order(SceneHitObjectKind kind) {
    switch (kind) {
        case SceneHitObjectKind::NodeBody:       return 0;
        case SceneHitObjectKind::ContentRegion:  return 1;
    }
    return 0;
}

struct SceneHitObject {
    SceneObjectId id;
    ui::InternedId node_id;
    ui::InternedId element_id;
    ui::InternedId region_id;
    SceneHitObjectKind kind = SceneHitObjectKind::ContentRegion;
    HitShapeKind shape = HitShapeKind::Rectangle;
    Rect bounds;
    std::vector<InteractionBinding> interactions;
};

struct SceneObjectRange {
    size_t offset = 0;
    size_t count = 0;
};

struct SceneNodeIndexEntry {
    ui::InternedId node_id;
    SceneObjectRange render_range;
    SceneObjectRange hit_range;
};

struct SemanticSceneSnapshot {
    std::vector<SceneRenderObject> render_objects;
    std::vector<SceneHitObject> hit_objects;
    std::vector<SceneNodeIndexEntry> node_index;
};

struct SemanticSceneNode {
    NodePresentation presentation;
    NodeSlotLayout layout;
};

SemanticSceneSnapshot build_semantic_scene_snapshot(const std::vector<SemanticSceneNode>& nodes);

SemanticSceneSnapshot build_semantic_scene_snapshot(const NodePresentation& presentation,
                                                    const NodeSlotLayout& layout);

const SceneRenderObject* find_render_object_by_id(const SemanticSceneSnapshot& snapshot, SceneObjectId id);
const SceneHitObject* find_hit_object_by_id(const SemanticSceneSnapshot& snapshot, SceneObjectId id);
const SceneNodeIndexEntry* find_scene_node_index(const SemanticSceneSnapshot& snapshot, ui::InternedId node_id);
const SceneHitObject* find_hit_object_by_region_id(const SemanticSceneSnapshot& snapshot, ui::InternedId region_id);
std::vector<const SceneRenderObject*> ordered_render_objects(const SemanticSceneSnapshot& snapshot);
std::vector<const SceneHitObject*> ordered_hit_objects(const SemanticSceneSnapshot& snapshot);
std::vector<const SceneHitObject*> hit_objects_for_node(const SemanticSceneSnapshot& snapshot, ui::InternedId node_id);
std::vector<const SceneHitObject*> hit_objects_for_region(const SemanticSceneSnapshot& snapshot, ui::InternedId region_id);

} // namespace editor::presentation
