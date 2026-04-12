#include "editor/visual/presentation/semantic_scene_input.h"

namespace editor::presentation {

SemanticSceneInputResult process_semantic_scene_input(const SemanticSceneSnapshot& snapshot,
                                                      const SemanticInteractionSession& current_session,
                                                      PointerPhase phase, ui::Pt point) {
    SemanticSceneInputResult result;

    // Step 1: hit-test
    result.hit = hit_test_semantic_scene(snapshot, point);

    // Step 2: resolve interaction
    result.resolved_request = resolve_semantic_interaction(result.hit, phase);

    // Step 3: reduce input
    result.reduced = reduce_semantic_input(current_session, phase, result.hit, result.resolved_request);

    return result;
}

} // namespace editor::presentation
