#include "editor/visual/presentation/semantic_input_machine.h"

#include <cassert>

namespace editor::presentation {

SemanticInputState SemanticInputMachine::derive_state(const SemanticInteractionSession& session) {
    if (!session.active) {
        return SemanticInputState::Idle;
    }

    switch (session.kind) {
        case InteractionKind::DragScalar:
        case InteractionKind::DragDiscrete:
            return SemanticInputState::Dragging;
        case InteractionKind::Press:
            return SemanticInputState::Pressed;
        case InteractionKind::Click:
        case InteractionKind::Release:
            // Active sessions never carry Click or Release kind.
            // The reducer ends the session for these kinds.
            assert(false && "active session with non-persistent InteractionKind");
            return SemanticInputState::Idle;
    }

    return SemanticInputState::Idle;
}

SemanticInputTransition SemanticInputMachine::derive_transition(SemanticInputState previous_state,
                                                                PointerPhase phase,
                                                                const SemanticSceneInputResult& result,
                                                                SemanticInputState next_state) {
    if (phase == PointerPhase::Release && previous_state != SemanticInputState::Idle &&
        next_state == SemanticInputState::Idle) {
        return SemanticInputTransition::EndedRelease;
    }

    if (result.reduced.emitted_requests.empty()) {
        return SemanticInputTransition::None;
    }

    const InteractionKind kind = result.reduced.emitted_requests.front().kind;
    switch (kind) {
        case InteractionKind::Press:
            return SemanticInputTransition::BeganPress;
        case InteractionKind::DragScalar:
        case InteractionKind::DragDiscrete:
            if (previous_state == SemanticInputState::Dragging) {
                return SemanticInputTransition::Continued;
            }
            return SemanticInputTransition::BeganDrag;
        case InteractionKind::Release:
            return SemanticInputTransition::EndedRelease;
        case InteractionKind::Click:
            return SemanticInputTransition::None;
    }

    return SemanticInputTransition::None;
}

SemanticInputTransition SemanticInputMachine::cancel() {
    const SemanticInputTransition transition = session_.active ? SemanticInputTransition::Cancelled
                                                               : SemanticInputTransition::None;
    reset();
    return transition;
}

SemanticInputMachineStepResult SemanticInputMachine::step(const SemanticSceneSnapshot& snapshot,
                                                          PointerPhase phase,
                                                          ui::Pt point) {
    const SemanticInputState previous_state = state_;

    SemanticInputMachineStepResult result;
    result.previous_state = previous_state;
    result.scene_result = process_semantic_scene_input(snapshot, session_, phase, point);
    session_ = result.scene_result.reduced.next_session;
    state_ = derive_state(session_);
    result.next_state = state_;
    result.transition = derive_transition(previous_state, phase, result.scene_result, state_);
    return result;
}

} // namespace editor::presentation
