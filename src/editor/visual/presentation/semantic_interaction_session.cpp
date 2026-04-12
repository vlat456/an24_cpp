#include "editor/visual/presentation/semantic_interaction_session.h"

namespace editor::presentation {

SemanticInteractionSession begin_semantic_interaction_session(const SemanticInteractionRequest& request) {
    SemanticInteractionSession session;
    session.active = true;
    session.node_id = request.node_id;
    session.element_id = request.element_id;
    session.region_id = request.region_id;
    session.action_id = request.action_id;
    session.kind = request.kind;
    session.min_value = request.min_value;
    session.max_value = request.max_value;
    session.step = request.step;
    return session;
}

SemanticInteractionSession end_semantic_interaction_session() {
    return SemanticInteractionSession();
}

bool session_matches_hit(const SemanticInteractionSession& session, const SemanticHitResult& hit) {
    if (!session.active) {
        return false;
    }

    const auto* content = std::get_if<SemanticHitContentRegion>(&hit);
    if (content == nullptr) {
        return false;
    }

    const SceneHitObject& object = *content->object;
    return object.node_id == session.node_id && object.element_id == session.element_id &&
           object.region_id == session.region_id;
}

std::optional<SemanticInteractionRequest> continue_semantic_interaction_session(
    const SemanticInteractionSession& session, PointerPhase phase) {
    if (!session.active) {
        return std::nullopt;
    }

    if (session.kind == InteractionKind::DragScalar || session.kind == InteractionKind::DragDiscrete) {
        if (phase != PointerPhase::Drag) {
            return std::nullopt;
        }

        SemanticInteractionRequest request;
        request.node_id = session.node_id;
        request.element_id = session.element_id;
        request.region_id = session.region_id;
        request.action_id = session.action_id;
        request.kind = session.kind;
        request.min_value = session.min_value;
        request.max_value = session.max_value;
        request.step = session.step;
        return request;
    }

    if (session.kind == InteractionKind::Press) {
        if (phase != PointerPhase::Release) {
            return std::nullopt;
        }

        SemanticInteractionRequest request;
        request.node_id = session.node_id;
        request.element_id = session.element_id;
        request.region_id = session.region_id;
        request.action_id = session.action_id;
        request.kind = InteractionKind::Release;
        return request;
    }

    return std::nullopt;
}

} // namespace editor::presentation
