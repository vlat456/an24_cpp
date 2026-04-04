#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/path/path.h"
#include <optional>

TEST(PathArena, RootPathHasKindRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    EXPECT_EQ(root.kind(), bp2::PathKind::Root);
}

TEST(PathArena, MakeNodeReturnsNodeKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    bp2::Path node = arena.make_node(root, interner.intern("battery1"));
    EXPECT_EQ(node.kind(), bp2::PathKind::Node);
    EXPECT_EQ(interner.resolve(node.segment()), "battery1");
}

TEST(PathArena, MakePortReturnsPortKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("bat1"));
    bp2::Path port = arena.make_port(node, interner.intern("v_out"));
    EXPECT_EQ(port.kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(port.segment()), "v_out");
}

TEST(PathArena, MakeNestedReturnsNestedKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path nested = arena.make_nested(arena.root(), interner.intern("sub1"));
    EXPECT_EQ(nested.kind(), bp2::PathKind::Nested);
}

TEST(PathArena, MakeWireReturnsWireKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path wire = arena.make_wire(arena.root(), interner.intern("w1"));
    EXPECT_EQ(wire.kind(), bp2::PathKind::Wire);
}

TEST(PathArena, ParentOfNodeIsRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    bp2::Path node = arena.make_node(root, interner.intern("r1"));
    EXPECT_EQ(arena.parent(node), root);
}

TEST(PathArena, ParentOfPortIsNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("r1"));
    bp2::Path port = arena.make_port(node, interner.intern("in"));
    EXPECT_EQ(arena.parent(port), node);
}

TEST(PathArena, ParentOfRootIsRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    EXPECT_EQ(arena.parent(arena.root()), arena.root());
}

TEST(PathToString, RootIsSlash) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    EXPECT_EQ(arena.to_string(arena.root()), "/");
}

TEST(PathToString, NodePath) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("bat1"));
    EXPECT_EQ(arena.to_string(node), "/bat1");
}

TEST(PathToString, PortPath) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("bat1"));
    bp2::Path port = arena.make_port(node, interner.intern("v_out"));
    EXPECT_EQ(arena.to_string(port), "/bat1:v_out");
}

TEST(PathToString, NestedNodePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path sub = arena.make_nested(arena.root(), interner.intern("sub1"));
    bp2::Path node = arena.make_node(sub, interner.intern("r1"));
    bp2::Path port = arena.make_port(node, interner.intern("in"));
    EXPECT_EQ(arena.to_string(port), "/sub1/r1:in");
}

TEST(PathToString, DeepNesting) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path s1 = arena.make_nested(arena.root(), interner.intern("a"));
    bp2::Path s2 = arena.make_nested(s1, interner.intern("b"));
    bp2::Path node = arena.make_node(s2, interner.intern("c"));
    EXPECT_EQ(arena.to_string(node), "/a/b/c");
}

TEST(PathParse, ParseRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Root);
}

TEST(PathParse, ParseNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/battery1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Node);
    EXPECT_EQ(interner.resolve(result->segment()), "battery1");
}

TEST(PathParse, ParsePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/bat1:v_out");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(result->segment()), "v_out");
}

TEST(PathParse, ParseNestedNodePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/sub1/r1:in");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(result->segment()), "in");
}

TEST(PathParse, RoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    std::string original = "/a/b/c:port";
    auto parsed = arena.parse(original);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(arena.to_string(*parsed), original);
}

TEST(PathParse, EmptyStringFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("");
    EXPECT_FALSE(result.has_value());
}

TEST(PathParse, NoLeadingSlashFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("bat1:v_out");
    EXPECT_FALSE(result.has_value());
}

// Step 1.8: Path equality and hash

TEST(PathEquality, SamePathsAreEqual) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("x"));
    EXPECT_EQ(a, b);
}

TEST(PathEquality, DifferentPathsAreNotEqual) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("y"));
    EXPECT_NE(a, b);
}

TEST(PathHash, SamePathsSameHash) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("x"));
    std::hash<bp2::Path> h;
    EXPECT_EQ(h(a), h(b));
}
