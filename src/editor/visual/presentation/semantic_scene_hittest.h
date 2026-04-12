#pragma once

#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "ui/math/pt.h"
#include <variant>

namespace editor::presentation {

struct SemanticHitEmpty {};

struct SemanticHitNodeBody {
    const SceneHitObject* object = nullptr;
};

struct SemanticHitContentRegion {
    const SceneHitObject* object = nullptr;
};

using SemanticHitResult = std::variant<SemanticHitEmpty, SemanticHitNodeBody, SemanticHitContentRegion>;

SemanticHitResult hit_test_semantic_scene(const SemanticSceneSnapshot& snapshot, ui::Pt point);

} // namespace editor::presentation
