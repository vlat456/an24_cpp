#pragma once

#include "editor/visual/presentation/semantic_input_machine.h"

namespace editor::presentation {

class SemanticCanvasHost {
public:
    SemanticCanvasHost() = default;

    void set_snapshot(SemanticSceneSnapshot snapshot) { snapshot_ = std::move(snapshot); }
    const SemanticSceneSnapshot& snapshot() const noexcept { return snapshot_; }

    SemanticInputState state() const noexcept { return machine_.state(); }
    const SemanticInteractionSession& session() const noexcept { return machine_.session(); }

    void reset() { machine_.reset(); }
    SemanticInputTransition cancel() { return machine_.cancel(); }

    SemanticInputMachineStepResult step(PointerPhase phase, ui::Pt point) {
        return machine_.step(snapshot_, phase, point);
    }

private:
    SemanticSceneSnapshot snapshot_;
    SemanticInputMachine machine_;
};

} // namespace editor::presentation
