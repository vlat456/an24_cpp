#pragma once

#include "editor/visual/presentation/semantic_interaction.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include <optional>

namespace editor::presentation {

struct SemanticInteractionSession {
    bool active = false;
    core::InternedId node_id;
    core::InternedId element_id;
    core::InternedId region_id;
    core::InternedId action_id;
    InteractionKind kind = InteractionKind::Click;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float step = 0.0f;
};

SemanticInteractionSession begin_semantic_interaction_session(const SemanticInteractionRequest& request);

bool session_matches_hit(const SemanticInteractionSession& session, const SemanticHitResult& hit);

std::optional<SemanticInteractionRequest> continue_semantic_interaction_session(
    const SemanticInteractionSession& session, PointerPhase phase);

SemanticInteractionSession end_semantic_interaction_session();

} // namespace editor::presentation
