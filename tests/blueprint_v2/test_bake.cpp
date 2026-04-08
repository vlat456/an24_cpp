#include <gtest/gtest.h>

#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/bake/bake_ops.h"

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                      const char* id,
                                      const char* type) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    return n;
}

TEST(BakeOps, BakeNestedReferenceToEmbedded) {
    ui::StringInterner I;
    bp2::BlueprintLibrary library;

    bp2::Blueprint lib;
    lib = lib.with_id(I.intern("sub_type"));
    lib = lib.with_node(make_node(I, "r1", "Resistor"));
    lib = lib.with_interface(bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    library.add(I.intern("sub_type"), lib);

    auto n = bp2::Blueprint::Nested::make_reference(
        I.intern("sub1"), I.intern("sub_type"), lib.iface());

    bp2::Blueprint root;
    root = root.with_nested(std::move(n));

    bp2::Blueprint baked = bp2::bake_nested(root, I.intern("sub1"), library);
    auto const* got = baked.find_nested(I.intern("sub1"));
    ASSERT_NE(got, nullptr);
    EXPECT_TRUE(got->is_embedded());
    EXPECT_NE(got->inline_def(), nullptr);
}

TEST(BakeOps, TryUnbakeReturnsReferenceWhenExactMatchFound) {
    ui::StringInterner I;
    bp2::BlueprintLibrary library;

    bp2::Blueprint lib;
    lib = lib.with_id(I.intern("sub_type"));
    lib = lib.with_node(make_node(I, "r1", "Resistor"));
    library.add(I.intern("sub_type"), lib);

    auto n = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub1"), I.intern("sub_type"),
        std::make_unique<bp2::Blueprint>(lib));

    bp2::Blueprint root;
    root = root.with_nested(std::move(n));

    auto unbaked = bp2::try_unbake(root, I.intern("sub1"), library);
    ASSERT_TRUE(unbaked.has_value());
    EXPECT_EQ(unbaked->referenced_id, I.intern("sub_type"));

    auto const* got = unbaked->blueprint.find_nested(I.intern("sub1"));
    ASSERT_NE(got, nullptr);
    EXPECT_FALSE(got->is_embedded());
    EXPECT_EQ(got->blueprint_id(), I.intern("sub_type"));
}

TEST(BakeOps, BakeAllRecursivelyEmbedsReferences) {
    ui::StringInterner I;
    bp2::BlueprintLibrary library;

    bp2::Blueprint grand;
    grand = grand.with_id(I.intern("grand_type"));
    grand = grand.with_node(make_node(I, "leaf", "Resistor"));
    library.add(I.intern("grand_type"), grand);

    bp2::Blueprint child;
    child = child.with_id(I.intern("child_type"));
    auto child_nested = bp2::Blueprint::Nested::make_reference(
        I.intern("g1"), I.intern("grand_type"), bp2::Interface());
    child = child.with_nested(std::move(child_nested));
    library.add(I.intern("child_type"), child);

    auto root_nested = bp2::Blueprint::Nested::make_reference(
        I.intern("c1"), I.intern("child_type"), bp2::Interface());

    bp2::Blueprint root;
    root = root.with_nested(std::move(root_nested));

    bp2::Blueprint baked = bp2::bake_all(root, library);
    auto const* c1 = baked.find_nested(I.intern("c1"));
    ASSERT_NE(c1, nullptr);
    EXPECT_TRUE(c1->is_embedded());
    ASSERT_NE(c1->inline_def(), nullptr);

    auto const* g1 = c1->inline_def()->find_nested(I.intern("g1"));
    ASSERT_NE(g1, nullptr);
    EXPECT_TRUE(g1->is_embedded());
}

TEST(BakeOps, BakeAllDetectsCyclicReferenceGraph) {
    ui::StringInterner I;
    bp2::BlueprintLibrary library;

    bp2::Blueprint a;
    a = a.with_id(I.intern("A"));
    auto a_to_b = bp2::Blueprint::Nested::make_reference(
        I.intern("to_b"), I.intern("B"), bp2::Interface());
    a = a.with_nested(std::move(a_to_b));

    bp2::Blueprint b;
    b = b.with_id(I.intern("B"));
    auto b_to_a = bp2::Blueprint::Nested::make_reference(
        I.intern("to_a"), I.intern("A"), bp2::Interface());
    b = b.with_nested(std::move(b_to_a));

    library.add(I.intern("A"), a);
    library.add(I.intern("B"), b);

    bp2::Blueprint root;
    auto root_to_a = bp2::Blueprint::Nested::make_reference(
        I.intern("root_to_a"), I.intern("A"), bp2::Interface());
    root = root.with_nested(std::move(root_to_a));

    EXPECT_THROW(bp2::bake_all(root, library), std::runtime_error);
}
