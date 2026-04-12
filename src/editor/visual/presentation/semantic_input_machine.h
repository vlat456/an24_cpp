#pragma once

#include "editor/visual/presentation/semantic_scene_input.h"

namespace editor::presentation {

enum class SemanticInputState {
    Idle,
    Pressed,
    Dragging,
};

enum class SemanticInputTransition {
    None,
    BeganPress,
    BeganDrag,
    Continued,
    EndedRelease,
    Cancelled,
};

struct SemanticInputMachineStepResult {
    SemanticInputState previous_state = SemanticInputState::Idle;
    SemanticInputState next_state = SemanticInputState::Idle;
    SemanticInputTransition transition = SemanticInputTransition::None;
    SemanticSceneInputResult scene_result;
};

class SemanticInputMachine {
public:
    SemanticInputMachine() = default;

    SemanticInputState state() const noexcept { return state_; }
    const SemanticInteractionSession& session() const noexcept { return session_; }
    void reset() {
        session_ = end_semantic_interaction_session();
        state_ = SemanticInputState::Idle;
    }
    SemanticInputTransition cancel();

    SemanticInputMachineStepResult step(const SemanticSceneSnapshot& snapshot, PointerPhase phase, ui::Pt point);

private:
    static SemanticInputState derive_state(const SemanticInteractionSession& session);
    static SemanticInputTransition derive_transition(SemanticInputState previous_state,
                                                     PointerPhase phase,
                                                     const SemanticSceneInputResult& result,
                                                     SemanticInputState next_state);

    SemanticInputState state_ = SemanticInputState::Idle;
    SemanticInteractionSession session_;
};

} // namespace editor::presentation
