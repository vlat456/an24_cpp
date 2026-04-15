#include "editor/visual/presentation/semantic_scene_hittest.h"
#include "editor/visual/presentation/hit_geometry.h"

namespace editor::presentation {

SemanticHitResult hit_test_semantic_scene(const SemanticSceneSnapshot& snapshot, ui::Pt point) {
    for (const SceneHitObject* obj : ordered_hit_objects(snapshot)) {
        if (!hit_geometry::point_hits_shape(point, obj->shape, obj->bounds)) {
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
