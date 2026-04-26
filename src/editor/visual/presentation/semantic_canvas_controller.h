#pragma once

#include "editor/visual/presentation/semantic_canvas_host.h"

#include <algorithm>

namespace editor::presentation {

enum class SemanticControlEventKind {
    None,
    Toggle,
    SetScalar,
    SetDiscrete,
};

struct SemanticControlEvent {
    SemanticControlEventKind kind = SemanticControlEventKind::None;
    core::InternedId node_id;
    float scalar_value = 0.0f;
    int discrete_value = 0;
};

struct SemanticScalarControlMapping {
    float primary_origin = 0.0f;
    float primary_min = 0.0f;
    float primary_max = 0.0f;
    float value_min = 0.0f;
    float value_max = 0.0f;
};

struct SemanticDiscreteControlMapping {
    float drag_origin_x = 0.0f;
    int start_value = 0;
    int value_count = 2;
    float pixels_per_step = 30.0f;
};

struct SemanticCanvasControllerResult {
    SemanticControlEvent control_event;
    SemanticInputMachineStepResult step_result;
};

class SemanticCanvasController {
public:
    SemanticCanvasController() = default;

    void set_snapshot(SemanticSceneSnapshot snapshot) { host_.set_snapshot(std::move(snapshot)); }
    const SemanticSceneSnapshot& snapshot() const noexcept { return host_.snapshot(); }

    SemanticInputState state() const noexcept { return host_.state(); }
    const SemanticInteractionSession& session() const noexcept { return host_.session(); }

    void reset() { host_.reset(); }
    SemanticInputTransition cancel() { return host_.cancel(); }

    void set_active_scalar_mapping(const SemanticScalarControlMapping& mapping) {
        active_scalar_mapping_ = mapping;
        active_mapping_kind_ = ActiveMappingKind::Scalar;
    }

    void set_active_discrete_mapping(const SemanticDiscreteControlMapping& mapping) {
        active_discrete_mapping_ = mapping;
        active_mapping_kind_ = ActiveMappingKind::Discrete;
    }

    void clear_active_mapping() {
        active_mapping_kind_ = ActiveMappingKind::None;
    }

    SemanticCanvasControllerResult on_pointer_press(ui::Pt point) {
        return build_result(host_.step(PointerPhase::Press, point));
    }

    SemanticCanvasControllerResult on_pointer_drag(ui::Pt point) {
        SemanticCanvasControllerResult result = build_result(host_.step(PointerPhase::Drag, point));
        if (active_mapping_kind_ == ActiveMappingKind::Scalar &&
            result.control_event.kind == SemanticControlEventKind::SetScalar) {
            result.control_event.scalar_value = compute_scalar_value(point, active_scalar_mapping_);
        } else if (active_mapping_kind_ == ActiveMappingKind::Discrete &&
                   result.control_event.kind == SemanticControlEventKind::SetDiscrete) {
            result.control_event.discrete_value = compute_discrete_value(point, active_discrete_mapping_);
        }
        return result;
    }

    SemanticCanvasControllerResult on_pointer_release(ui::Pt point) {
        return build_result(host_.step(PointerPhase::Release, point));
    }

private:
    enum class ActiveMappingKind {
        None,
        Scalar,
        Discrete,
    };

    static float compute_scalar_value(ui::Pt point, const SemanticScalarControlMapping& mapping) {
        const float local_primary = point.x - mapping.primary_origin;
        const float range = mapping.primary_max - mapping.primary_min;
        const float t = (range > 1e-6f)
            ? std::clamp((local_primary - mapping.primary_min) / range, 0.0f, 1.0f)
            : 0.0f;
        return mapping.value_min + t * (mapping.value_max - mapping.value_min);
    }

    static int compute_discrete_value(ui::Pt point, const SemanticDiscreteControlMapping& mapping) {
        const float dx = point.x - mapping.drag_origin_x;
        const int delta_steps = static_cast<int>(dx / mapping.pixels_per_step);
        return std::clamp(mapping.start_value + delta_steps, 0, mapping.value_count - 1);
    }

    SemanticCanvasControllerResult build_result(SemanticInputMachineStepResult step_result) const {
        SemanticCanvasControllerResult result;
        if (!step_result.scene_result.reduced.emitted_requests.empty()) {
            const SemanticInteractionRequest& request =
                step_result.scene_result.reduced.emitted_requests.front();
            result.control_event.node_id = request.node_id;
            switch (request.kind) {
                case InteractionKind::Click:
                    result.control_event.kind = SemanticControlEventKind::Toggle;
                    break;
                case InteractionKind::DragScalar:
                    result.control_event.kind = SemanticControlEventKind::SetScalar;
                    break;
                case InteractionKind::DragDiscrete:
                    result.control_event.kind = SemanticControlEventKind::SetDiscrete;
                    break;
                case InteractionKind::Press:
                case InteractionKind::Release:
                    break;
            }
        }
        result.step_result = std::move(step_result);
        return result;
    }

    SemanticCanvasHost host_;
    SemanticScalarControlMapping active_scalar_mapping_;
    SemanticDiscreteControlMapping active_discrete_mapping_;
    ActiveMappingKind active_mapping_kind_ = ActiveMappingKind::None;
};

} // namespace editor::presentation
