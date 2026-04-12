#include "editor/visual/presentation/semantic_interaction.h"

namespace editor::presentation {

namespace {

bool binding_matches_phase(const InteractionBinding& binding, PointerPhase phase) {
    switch (phase) {
        case PointerPhase::Press:
            return binding.kind == InteractionKind::Click || binding.kind == InteractionKind::Press;
        case PointerPhase::Drag:
            return binding.kind == InteractionKind::DragScalar || binding.kind == InteractionKind::DragDiscrete;
        case PointerPhase::Release:
            return binding.kind == InteractionKind::Release;
    }
    return false;
}

std::optional<SemanticInteractionRequest> resolve_from_object(const SceneHitObject& object,
                                                              PointerPhase phase) {
    for (const InteractionBinding& binding : object.interactions) {
        if (!binding_matches_phase(binding, phase)) {
            continue;
        }

        SemanticInteractionRequest request;
        request.node_id = object.node_id;
        request.element_id = object.element_id;
        request.region_id = object.region_id;
        request.action_id = binding.action_id;
        request.kind = binding.kind;
        request.min_value = binding.min_value;
        request.max_value = binding.max_value;
        request.step = binding.step;
        return request;
    }

    return std::nullopt;
}

} // namespace

std::optional<SemanticInteractionRequest> resolve_semantic_interaction(const SemanticHitResult& hit,
                                                                       PointerPhase phase) {
    if (const auto* content = std::get_if<SemanticHitContentRegion>(&hit)) {
        return resolve_from_object(*content->object, phase);
    }
    return std::nullopt;
}

} // namespace editor::presentation
