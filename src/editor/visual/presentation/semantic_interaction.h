#pragma once

#include "editor/visual/presentation/semantic_scene_hittest.h"
#include <optional>

namespace editor::presentation {

enum class PointerPhase {
    Press,
    Drag,
    Release,
};

struct SemanticInteractionRequest {
    ui::InternedId node_id;
    ui::InternedId element_id;
    ui::InternedId region_id;
    ui::InternedId action_id;
    InteractionKind kind = InteractionKind::Click;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float step = 0.0f;
};

std::optional<SemanticInteractionRequest> resolve_semantic_interaction(const SemanticHitResult& hit,
                                                                       PointerPhase phase);

} // namespace editor::presentation
