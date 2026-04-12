#pragma once

#include "editor/visual/presentation/semantic_interaction.h"
#include "editor/visual/presentation/semantic_interaction_session.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include <vector>

namespace editor::presentation {

struct SemanticInputStepResult {
    SemanticInteractionSession next_session;
    std::vector<SemanticInteractionRequest> emitted_requests;
};

SemanticInputStepResult reduce_semantic_input(const SemanticInteractionSession& current_session,
                                              PointerPhase phase, const SemanticHitResult& hit,
                                              const std::optional<SemanticInteractionRequest>& resolved_request);

} // namespace editor::presentation
