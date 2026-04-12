#include "editor/visual/presentation/semantic_input_reducer.h"

namespace editor::presentation {

SemanticInputStepResult reduce_semantic_input(const SemanticInteractionSession& current_session,
                                              PointerPhase phase, const SemanticHitResult& hit,
                                              const std::optional<SemanticInteractionRequest>& resolved_request) {
    SemanticInputStepResult result;
    result.next_session = current_session;

    // Rule 1: If resolved_request has a value, handle it with priority
    if (resolved_request.has_value()) {
        result.emitted_requests.push_back(resolved_request.value());

        if (resolved_request->kind == InteractionKind::Press ||
            resolved_request->kind == InteractionKind::DragScalar ||
            resolved_request->kind == InteractionKind::DragDiscrete) {
            result.next_session = begin_semantic_interaction_session(resolved_request.value());
        } else {
            result.next_session = end_semantic_interaction_session();
        }

        return result;
    }

    // Rule 2: Ask current session for continuation
    std::optional<SemanticInteractionRequest> continuation =
        continue_semantic_interaction_session(current_session, phase);

    if (continuation.has_value()) {
        result.emitted_requests.push_back(continuation.value());

        if (continuation->kind == InteractionKind::Release) {
            result.next_session = end_semantic_interaction_session();
        } else {
            result.next_session = current_session;
        }

        return result;
    }

    // Rule 3: If phase is Release with no continuation, always clear session
    if (phase == PointerPhase::Release) {
        result.next_session = end_semantic_interaction_session();
        return result;
    }

    // Rule 4: If session is active but doesn't match hit, keep it unchanged
    if (current_session.active && !session_matches_hit(current_session, hit)) {
        result.next_session = current_session;
        return result;
    }

    // Rule 5: Default case - return result with unchanged session and no emitted requests
    return result;
}

} // namespace editor::presentation
