#pragma once

#include "editor/visual/presentation/semantic_scene_snapshot.h"

namespace semantic_test {

inline editor::presentation::SceneHitObject make_content_region(
    core::InternedId node_id, core::InternedId element_id,
    core::InternedId region_id, const ui::Rect& bounds) {
    editor::presentation::SceneHitObject object;
    object.node_id = node_id;
    object.element_id = element_id;
    object.region_id = region_id;
    object.kind = editor::presentation::SceneHitObjectKind::ContentRegion;
    object.bounds = bounds;
    return object;
}

inline editor::presentation::InteractionBinding make_binding(
    core::InternedId region_id, editor::presentation::InteractionKind kind,
    core::InternedId action_id) {
    editor::presentation::InteractionBinding binding;
    binding.region_id = region_id;
    binding.kind = kind;
    binding.action_id = action_id;
    return binding;
}

} // namespace semantic_test
