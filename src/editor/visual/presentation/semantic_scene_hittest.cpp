#include "editor/visual/presentation/semantic_scene_hittest.h"

#include <algorithm>
#include <cmath>

namespace editor::presentation {

namespace {

bool point_in_rectangle(ui::Pt point, const Rect& bounds) {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.w &&
           point.y >= bounds.y && point.y <= bounds.y + bounds.h;
}

bool point_in_inscribed_circle(ui::Pt point, const Rect& bounds) {
    float center_x = bounds.x + bounds.w / 2.0f;
    float center_y = bounds.y + bounds.h / 2.0f;
    float radius = std::min(bounds.w, bounds.h) / 2.0f;
    
    float dx = point.x - center_x;
    float dy = point.y - center_y;
    float dist_sq = dx * dx + dy * dy;
    
    return dist_sq <= radius * radius;
}

bool point_hits_shape(ui::Pt point, const SceneHitObject& obj) {
    switch (obj.shape) {
        case HitShapeKind::Rectangle:
            return point_in_rectangle(point, obj.bounds);
        case HitShapeKind::Circle:
            return point_in_inscribed_circle(point, obj.bounds);
    }

    return false;
}

} // namespace

SemanticHitResult hit_test_semantic_scene(const SemanticSceneSnapshot& snapshot, ui::Pt point) {
    for (const SceneHitObject* obj : ordered_hit_objects(snapshot)) {
        if (!point_hits_shape(point, *obj)) {
            continue;
        }

        switch (obj->kind) {
            case SceneHitObjectKind::ContentRegion:
                return SemanticHitContentRegion{.object = obj};
            case SceneHitObjectKind::NodeBody:
                return SemanticHitNodeBody{.object = obj};
        }
    }

    return SemanticHitEmpty{};
}

} // namespace editor::presentation
