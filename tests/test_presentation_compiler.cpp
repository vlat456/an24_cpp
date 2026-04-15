#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "editor/visual/presentation/node_presentation.h"
#include "ui/core/interned_id.h"

using namespace editor::presentation;

namespace {

bp2::Blueprint::Node make_test_node(ui::InternedId id, const std::string& name,
                                    bp2::NodeContentType content_type) {
    bp2::Blueprint::Node node;
    node.semantic.id = id;
    node.view.name = name;
    node.view.content_type = content_type;
    return node;
}

PresentationNode make_empty_fragment(const bp2::Blueprint::Node& /*node*/, ui::InternedId /*type_id*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(40);
    root.layout = LayoutKind::Column;
    return root;
}

PresentationNode make_custom_fragment(const bp2::Blueprint::Node& node, ui::InternedId /*type_id*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(50);
    root.layout = LayoutKind::Overlay;

    PresentationNode title;
    title.element_id = ui::InternedId(51);
    PaintCommand title_cmd;
    title_cmd.id = ui::InternedId(52);
    title_cmd.kind = PaintPrimitiveKind::Text;
    title_cmd.text = node.view.name;
    title.paint.push_back(std::move(title_cmd));

    PresentationNode badge;
    badge.element_id = ui::InternedId(53);
    PaintCommand badge_cmd;
    badge_cmd.id = ui::InternedId(54);
    badge_cmd.kind = PaintPrimitiveKind::Circle;
    badge.paint.push_back(std::move(badge_cmd));

    HitRegion badge_hit;
    badge_hit.id = ui::InternedId(55);
    badge_hit.kind = HitShapeKind::Circle;
    badge.hit_regions.push_back(badge_hit);

    InteractionBinding badge_click;
    badge_click.region_id = badge_hit.id;
    badge_click.kind = InteractionKind::Click;
    badge_click.action_id = ui::InternedId(56);
    badge.interactions.push_back(badge_click);

    root.children.push_back(std::move(title));
    root.children.push_back(std::move(badge));
    return root;
}

} // namespace

TEST(NodePresentationCompiler, PreservesNodeIdentityAndTitle) {
    auto node = make_test_node(ui::InternedId(1), "Generator", bp2::NodeContentType::None);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(100), NodePresenter{NodeFrameKind::Standard, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(100));

    EXPECT_EQ(presentation.node_id, ui::InternedId(1));
    EXPECT_EQ(presentation.shell.title, "Generator");
}

TEST(NodePresentationCompiler, MapsRefRenderHintToReferenceFrame) {
    auto node = make_test_node(ui::InternedId(2), "Ref", bp2::NodeContentType::None);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(101), NodePresenter{NodeFrameKind::Reference, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(101));

    EXPECT_EQ(presentation.shell.frame_kind, NodeFrameKind::Reference);
}

TEST(NodePresentationCompiler, MapsBusRenderHintToBusFrame) {
    auto node = make_test_node(ui::InternedId(3), "Bus", bp2::NodeContentType::None);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(102), NodePresenter{NodeFrameKind::Bus, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(102));

    EXPECT_EQ(presentation.shell.frame_kind, NodeFrameKind::Bus);
}

TEST(NodePresentationCompiler, RegisteredPresenterProducesArbitraryFragmentTree) {
    auto node = make_test_node(ui::InternedId(4), "Throttle", bp2::NodeContentType::Slider);
    node.view.content_label = "THR";
    node.view.content_min = 0.0f;
    node.view.content_max = 100.0f;
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(103), NodePresenter{NodeFrameKind::Standard, &make_custom_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(103));

    EXPECT_EQ(presentation.content.layout, LayoutKind::Overlay);
    ASSERT_EQ(presentation.content.children.size(), 2u);

    const PresentationNode& title = presentation.content.children[0];
    ASSERT_EQ(title.paint.size(), 1u);
    EXPECT_EQ(title.paint[0].kind, PaintPrimitiveKind::Text);
    EXPECT_EQ(title.paint[0].text, "Throttle");

    const PresentationNode& badge = presentation.content.children[1];
    ASSERT_EQ(badge.paint.size(), 1u);
    EXPECT_EQ(badge.paint[0].kind, PaintPrimitiveKind::Circle);
    ASSERT_EQ(badge.hit_regions.size(), 1u);
    EXPECT_EQ(badge.hit_regions[0].kind, HitShapeKind::Circle);
    ASSERT_EQ(badge.interactions.size(), 1u);
    EXPECT_EQ(badge.interactions[0].kind, InteractionKind::Click);
}

TEST(NodePresentationCompiler, RegisteredPresenterAllowsNoContentChildren) {
    auto node = make_test_node(ui::InternedId(5), "Empty", bp2::NodeContentType::None);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(104), NodePresenter{NodeFrameKind::Standard, &make_empty_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(104));

    EXPECT_TRUE(presentation.content.children.empty());
}

TEST(NodePresentationCompiler, RegisteredPresenterCanEmitSingleLabelOnlyFragment) {
    auto node = make_test_node(ui::InternedId(6), "Status", bp2::NodeContentType::Text);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(105), NodePresenter{NodeFrameKind::Annotation, &make_custom_fragment});
    NodePresentationCompileContext ctx{&registry};

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(105));

    ASSERT_EQ(presentation.content.children.size(), 2u);
    EXPECT_EQ(presentation.content.children[0].paint[0].text, "Status");
    EXPECT_EQ(presentation.shell.frame_kind, NodeFrameKind::Annotation);
}

TEST(NodePresentationCompiler, RegistryOverridesDefaultContentCompilation) {
    auto node = make_test_node(ui::InternedId(8), "Custom", bp2::NodeContentType::Slider);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(500), NodePresenter{NodeFrameKind::Group, &make_custom_fragment});

    NodePresentationCompileContext ctx;
    ctx.registry = &registry;

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(500));

    EXPECT_EQ(presentation.content.layout, LayoutKind::Overlay);
    ASSERT_EQ(presentation.content.children.size(), 2u);
    EXPECT_EQ(presentation.content.children[0].paint[0].text, "Custom");
    EXPECT_EQ(presentation.content.children[1].paint[0].kind, PaintPrimitiveKind::Circle);
    EXPECT_EQ(presentation.content.children[1].hit_regions[0].kind, HitShapeKind::Circle);
    EXPECT_EQ(presentation.shell.frame_kind, NodeFrameKind::Group);
}

TEST(NodePresentationCompiler, RegistryCanReplacePresenterForSameType) {
    auto node = make_test_node(ui::InternedId(9), "Replace", bp2::NodeContentType::None);
    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(600), NodePresenter{NodeFrameKind::Standard, &make_empty_fragment});
    registry.register_presenter(ui::InternedId(600), NodePresenter{NodeFrameKind::Reference, &make_custom_fragment});

    NodePresentationCompileContext ctx;
    ctx.registry = &registry;

    NodePresentation presentation = compile_node_presentation(ctx, node, ui::InternedId(600));

    EXPECT_EQ(presentation.content.layout, LayoutKind::Overlay);
    EXPECT_EQ(presentation.shell.frame_kind, NodeFrameKind::Reference);
}

TEST(NodePresentationCompiler, RegistryReturnsNullForMissingType) {
    NodePresenterRegistry registry;
    EXPECT_EQ(registry.find_presenter(ui::InternedId(999)), nullptr);
}

TEST(NodePresentationCompiler, MissingPresenterDiesInDebug) {
#ifndef NDEBUG
    bp2::Blueprint::Node node = make_test_node(ui::InternedId(10), "Missing", bp2::NodeContentType::None);
    NodePresenterRegistry registry;

    EXPECT_DEATH((void)compile_node_presentation(NodePresentationCompileContext{&registry}, node, ui::InternedId(700)), "");
#endif
}
