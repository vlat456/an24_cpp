#include "editor/visual/presentation/semantic_scene_snapshot.h"

#include <algorithm>
#include <cassert>

namespace editor::presentation {

namespace {

const ui::Rect* find_slot_bounds(const NodeSlotLayout& layout, NodeSlot slot) {
    for (const SlotAssignment& assignment : layout.slots) {
        if (assignment.slot == slot) {
            return &assignment.bounds;
        }
    }
    return nullptr;
}

const ui::Rect* find_element_bounds(const NodeSlotLayout& layout, ui::InternedId element_id) {
    for (const FragmentPlacement& placement : layout.placements) {
        if (placement.element_id == element_id) {
            return &placement.bounds;
        }
    }
    return nullptr;
}

void append_content_objects(const NodePresentation& presentation,
                            const NodeSlotLayout& layout,
                            const PresentationNode& node,
                            SemanticSceneSnapshot& snapshot,
                            uint32_t& next_id) {
    const ui::Rect* bounds = find_element_bounds(layout, node.element_id);
    assert(bounds != nullptr);

    for (const PaintCommand& paint : node.paint) {
        SceneRenderObject render_object;
        render_object.id = SceneObjectId(next_id++);
        render_object.node_id = presentation.node_id;
        render_object.element_id = node.element_id;
        render_object.kind = SceneRenderObjectKind::ContentPaint;
        render_object.frame_kind = presentation.shell.frame_kind;
        render_object.primitive = paint.kind;
        render_object.bounds = *bounds;
        render_object.text = paint.text;
        render_object.fill_color = paint.fill_color;
        render_object.stroke_color = paint.stroke_color;
        render_object.stroke_width = paint.stroke_width;
        render_object.inset = paint.inset;
        render_object.text_size = paint.text_size;
        snapshot.render_objects.push_back(std::move(render_object));
    }

    for (const HitRegion& region : node.hit_regions) {
        SceneHitObject hit_object;
        hit_object.id = SceneObjectId(next_id++);
        hit_object.node_id = presentation.node_id;
        hit_object.element_id = node.element_id;
        hit_object.region_id = region.id;
        hit_object.kind = SceneHitObjectKind::ContentRegion;
        hit_object.shape = region.kind;
        hit_object.bounds = *bounds;

        for (const InteractionBinding& binding : node.interactions) {
            if (binding.region_id == region.id) {
                hit_object.interactions.push_back(binding);
            }
        }

        snapshot.hit_objects.push_back(std::move(hit_object));
    }

    for (const PresentationNode& child : node.children) {
        append_content_objects(presentation, layout, child, snapshot, next_id);
    }
}

void append_node_snapshot(const NodePresentation& presentation,
                          const NodeSlotLayout& layout,
                          SemanticSceneSnapshot& snapshot,
                          uint32_t& next_id) {
    SceneNodeIndexEntry entry;
    entry.node_id = presentation.node_id;
    entry.render_range.offset = snapshot.render_objects.size();
    entry.hit_range.offset = snapshot.hit_objects.size();

    const ui::Rect* header = find_slot_bounds(layout, NodeSlot::Header);
    const ui::Rect* body = find_slot_bounds(layout, NodeSlot::Body);
    const ui::Rect* footer = find_slot_bounds(layout, NodeSlot::Footer);
    assert(header != nullptr);
    assert(body != nullptr);

    SceneRenderObject frame;
    frame.id = SceneObjectId(next_id++);
    frame.node_id = presentation.node_id;
    frame.kind = SceneRenderObjectKind::NodeFrame;
    frame.frame_kind = presentation.shell.frame_kind;
    frame.primitive = PaintPrimitiveKind::Rectangle;
    frame.bounds = layout.node_bounds;
    snapshot.render_objects.push_back(std::move(frame));

    SceneRenderObject title;
    title.id = SceneObjectId(next_id++);
    title.node_id = presentation.node_id;
    title.kind = SceneRenderObjectKind::NodeTitle;
    title.frame_kind = presentation.shell.frame_kind;
    title.primitive = PaintPrimitiveKind::Text;
    title.bounds = *header;
    title.text = presentation.shell.title;
    snapshot.render_objects.push_back(std::move(title));

    if (!presentation.shell.type_name.empty() && footer != nullptr && footer->h > 0.0f) {
        SceneRenderObject footer_label;
        footer_label.id = SceneObjectId(next_id++);
        footer_label.node_id = presentation.node_id;
        footer_label.kind = SceneRenderObjectKind::NodeFooter;
        footer_label.frame_kind = presentation.shell.frame_kind;
        footer_label.primitive = PaintPrimitiveKind::Text;
        footer_label.bounds = *footer;
        footer_label.text = presentation.shell.type_name;
        snapshot.render_objects.push_back(std::move(footer_label));
    }

    SceneHitObject node_body;
    node_body.id = SceneObjectId(next_id++);
    node_body.node_id = presentation.node_id;
    node_body.kind = SceneHitObjectKind::NodeBody;
    node_body.shape = HitShapeKind::Rectangle;
    node_body.bounds = *body;
    snapshot.hit_objects.push_back(std::move(node_body));

    append_content_objects(presentation, layout, presentation.content, snapshot, next_id);

    entry.render_range.count = snapshot.render_objects.size() - entry.render_range.offset;
    entry.hit_range.count = snapshot.hit_objects.size() - entry.hit_range.offset;
    snapshot.node_index.push_back(entry);
}

} // namespace

SemanticSceneSnapshot build_semantic_scene_snapshot(const std::vector<SemanticSceneNode>& nodes) {
    SemanticSceneSnapshot snapshot;
    uint32_t next_id = 1;

    for (const SemanticSceneNode& node : nodes) {
        append_node_snapshot(node.presentation, node.layout, snapshot, next_id);
    }

    return snapshot;
}

SemanticSceneSnapshot build_semantic_scene_snapshot(const NodePresentation& presentation,
                                                    const NodeSlotLayout& layout) {
    return build_semantic_scene_snapshot(std::vector<SemanticSceneNode>{{presentation, layout}});
}

const SceneRenderObject* find_render_object_by_id(const SemanticSceneSnapshot& snapshot, SceneObjectId id) {
    for (const SceneRenderObject& object : snapshot.render_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

const SceneHitObject* find_hit_object_by_id(const SemanticSceneSnapshot& snapshot, SceneObjectId id) {
    for (const SceneHitObject& object : snapshot.hit_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

const SceneNodeIndexEntry* find_scene_node_index(const SemanticSceneSnapshot& snapshot, ui::InternedId node_id) {
    for (const SceneNodeIndexEntry& entry : snapshot.node_index) {
        if (entry.node_id == node_id) {
            return &entry;
        }
    }
    return nullptr;
}

const SceneHitObject* find_hit_object_by_region_id(const SemanticSceneSnapshot& snapshot, ui::InternedId region_id) {
    for (const SceneHitObject& object : snapshot.hit_objects) {
        if (object.region_id == region_id) {
            return &object;
        }
    }
    return nullptr;
}

std::vector<const SceneRenderObject*> ordered_render_objects(const SemanticSceneSnapshot& snapshot) {
    std::vector<const SceneRenderObject*> ordered;
    ordered.reserve(snapshot.render_objects.size());

    for (const SceneRenderObject& object : snapshot.render_objects) {
        ordered.push_back(&object);
    }

    std::stable_sort(ordered.begin(), ordered.end(),
        [](const SceneRenderObject* lhs, const SceneRenderObject* rhs) {
            return render_layer_order(lhs->kind) < render_layer_order(rhs->kind);
        });

    return ordered;
}

std::vector<const SceneHitObject*> ordered_hit_objects(const SemanticSceneSnapshot& snapshot) {
    std::vector<const SceneHitObject*> ordered;
    ordered.reserve(snapshot.hit_objects.size());

    for (auto it = snapshot.hit_objects.rbegin(); it != snapshot.hit_objects.rend(); ++it) {
        ordered.push_back(&*it);
    }

    std::stable_sort(ordered.begin(), ordered.end(),
        [](const SceneHitObject* lhs, const SceneHitObject* rhs) {
            return hit_layer_order(lhs->kind) > hit_layer_order(rhs->kind);
        });

    return ordered;
}

std::vector<const SceneHitObject*> hit_objects_for_node(const SemanticSceneSnapshot& snapshot, ui::InternedId node_id) {
    std::vector<const SceneHitObject*> hits;
    const SceneNodeIndexEntry* entry = find_scene_node_index(snapshot, node_id);
    if (entry == nullptr) {
        return hits;
    }

    hits.reserve(entry->hit_range.count);
    for (size_t i = 0; i < entry->hit_range.count; ++i) {
        hits.push_back(&snapshot.hit_objects[entry->hit_range.offset + i]);
    }
    return hits;
}

std::vector<const SceneHitObject*> hit_objects_for_region(const SemanticSceneSnapshot& snapshot, ui::InternedId region_id) {
    std::vector<const SceneHitObject*> hits;
    for (const SceneHitObject& object : snapshot.hit_objects) {
        if (object.region_id == region_id) {
            hits.push_back(&object);
        }
    }
    return hits;
}

} // namespace editor::presentation
