#include "editor/visual/presentation/node_presentation.h"

namespace editor::presentation {

namespace {

} // namespace

void NodePresenterRegistry::register_presenter(ui::InternedId type_id, NodePresenter presenter) {
    assert(presenter.content != nullptr);
    presenters_[type_id] = std::move(presenter);
}

const NodePresenter* NodePresenterRegistry::find_presenter(ui::InternedId type_id) const {
    const auto it = presenters_.find(type_id);
    return it == presenters_.end() ? nullptr : &it->second;
}

NodePresentation compile_node_presentation(const NodePresentationCompileContext& ctx,
                                           const bp2::Blueprint::Node& node,
                                           ui::InternedId type_id) {
    assert(ctx.registry != nullptr);
    const NodePresenter* presenter = ctx.registry->find_presenter(type_id);
    assert(presenter != nullptr);
    assert(presenter->content != nullptr);

    NodePresentation presentation;
    presentation.node_id = node.semantic.id;
    presentation.shell.frame_kind = presenter->frame_kind;
    presentation.shell.title = node.view.name;
    presentation.content = presenter->content(node, type_id);

    return presentation;
}

} // namespace editor::presentation
