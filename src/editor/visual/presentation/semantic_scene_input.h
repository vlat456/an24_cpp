#pragma once

#include "editor/visual/presentation/semantic_input_reducer.h"
#include "editor/visual/presentation/semantic_interaction.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include "ui/math/pt.h"
#include <optional>

namespace editor::presentation {

struct SemanticSceneInputResult {
    SemanticHitResult hit;
    std::optional<SemanticInteractionRequest> resolved_request;
    SemanticInputStepResult reduced;
};

SemanticSceneInputResult process_semantic_scene_input(const SemanticSceneSnapshot& snapshot,
                                                      const SemanticInteractionSession& current_session,
                                                      PointerPhase phase, ui::Pt point);

} // namespace editor::presentation
