#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <cassert>
#include <unordered_map>
#include <string>
#include <vector>

namespace editor::presentation {

enum class NodeFrameKind {
    Standard,
    Reference,
    Bus,
    Group,
    Annotation,
};

enum class LayoutKind {
    None,
    Row,
    Column,
    Overlay,
};

enum class PaintPrimitiveKind {
    Text,
    Rectangle,
    Circle,
    Arc,
    Line,
};

struct PaintCommand {
    ui::InternedId id;
    PaintPrimitiveKind kind = PaintPrimitiveKind::Text;
    std::string text;
    uint32_t fill_color = 0;
    uint32_t stroke_color = 0;
    float stroke_width = 0.0f;
    float inset = 0.0f;
    float text_size = 0.0f;
};

enum class HitShapeKind {
    Rectangle,
    Circle,
};

struct HitRegion {
    ui::InternedId id;
    HitShapeKind kind = HitShapeKind::Rectangle;
};

enum class InteractionKind {
    Click,
    Press,
    Release,
    DragScalar,
    DragDiscrete,
};

struct InteractionBinding {
    ui::InternedId region_id;
    InteractionKind kind = InteractionKind::Click;
    ui::InternedId action_id;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float step = 0.0f;
};

struct PresentationNode {
    ui::InternedId element_id;
    LayoutKind layout = LayoutKind::None;
    float gap = 0.0f;
    std::vector<PaintCommand> paint;
    std::vector<HitRegion> hit_regions;
    std::vector<InteractionBinding> interactions;
    std::vector<PresentationNode> children;
};

struct NodeShellModel {
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    std::string title;
};

/// Content presenters remain stateless function pointers for now. Widen this
/// contract before broader adoption if presenters need injected dependencies.
using ContentPresenterFn = PresentationNode (*)(const bp2::Blueprint::Node& node, ui::InternedId type_id);

struct NodePresenter {
    NodeFrameKind frame_kind = NodeFrameKind::Standard;
    ContentPresenterFn content = nullptr;
};

class NodePresenterRegistry {
public:
    void register_presenter(ui::InternedId type_id, NodePresenter presenter);
    const NodePresenter* find_presenter(ui::InternedId type_id) const;

private:
    std::unordered_map<ui::InternedId, NodePresenter> presenters_;
};

struct NodePresentationCompileContext {
    const NodePresenterRegistry* registry = nullptr;
};

struct NodePresentation {
    ui::InternedId node_id;
    NodeShellModel shell;
    PresentationNode content;
};

NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const bp2::Blueprint::Node& node,
                                           ui::InternedId type_id);

} // namespace editor::presentation
