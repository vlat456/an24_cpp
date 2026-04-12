#pragma once

#include "editor/visual/presentation/semantic_scene_snapshot.h"

namespace semantic_test {

inline editor::presentation::SceneHitObject make_content_region(
    ui::InternedId node_id, ui::InternedId element_id,
    ui::InternedId region_id, const editor::presentation::Rect& bounds) {
    editor::presentation::SceneHitObject object;
    object.node_id = node_id;
    object.element_id = element_id;
    object.region_id = region_id;
    object.kind = editor::presentation::SceneHitObjectKind::ContentRegion;
    object.bounds = bounds;
    return object;
}

inline editor::presentation::InteractionBinding make_binding(
    ui::InternedId region_id, editor::presentation::InteractionKind kind,
    ui::InternedId action_id) {
    editor::presentation::InteractionBinding binding;
    binding.region_id = region_id;
    binding.kind = kind;
    binding.action_id = action_id;
    return binding;
}

} // namespace semantic_test
