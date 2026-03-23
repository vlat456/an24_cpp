#include <gtest/gtest.h>

#include "blueprint_v2/path/path.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/invariant_checker.h"

using bp2::Direction;
using bp2::Interface;
using bp2::Path;
using bp2::PathArena;
using bp2::PathResolver;
using bp2::PathKind;
using bp2::PortDescriptor;
using bp2::WireValidator;

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                      const char* id,
                                      const char* type) {
    bp2::Blueprint::Node n;
    n.id = I.intern(id);
    n.type = I.intern(type);
    return n;
}

TEST(PathResolver, ResolveNodePortOnRoot) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    Path node = arena.make_node(arena.root(), I.intern("bat1"));
    Path port = arena.make_port(node, I.intern("v_out"));

    PathResolver resolver;
    auto rp = resolver.resolve(port, bp, arena, reg);
    ASSERT_TRUE(rp.has_value());
    EXPECT_EQ(rp->port.domain, Domain::Electrical);
    EXPECT_EQ(rp->port.direction, Direction::Output);
    EXPECT_FALSE(rp->is_boundary);
}

TEST(PathResolver, ResolveRootInterfacePort) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_interface(Interface({
        {I.intern("v_in"), Domain::Electrical, Direction::Input},
    }));

    Path iface = arena.make_port(arena.root(), I.intern("v_in"));
    PathResolver resolver;
    auto rp = resolver.resolve(iface, bp, arena, reg);
    ASSERT_TRUE(rp.has_value());
    EXPECT_TRUE(rp->is_boundary);
    EXPECT_EQ(rp->port.direction, Direction::Input);
}

TEST(PathResolver, ResolveNestedInterfacePort) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("in"), Domain::Electrical, Direction::Input},
        {I.intern("out"), Domain::Electrical, Direction::Output},
    }));

    bp2::Blueprint::Nested n;
    n.id = I.intern("sub1");
    n.embedded = true;
    n.inline_def = std::make_unique<bp2::Blueprint>(inner);
    n.iface = inner.iface();

    bp2::Blueprint root;
    root = root.with_nested(std::move(n));

    Path nested = arena.make_nested(arena.root(), I.intern("sub1"));
    Path iface = arena.make_port(nested, I.intern("in"));

    PathResolver resolver;
    auto rp = resolver.resolve(iface, root, arena, reg);
    ASSERT_TRUE(rp.has_value());
    EXPECT_TRUE(rp->is_boundary);
    EXPECT_EQ(rp->port.direction, Direction::Input);
}

TEST(PathResolver, CanConnectRejectsBoundarySkipAcrossNestedScopes) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_node(make_node(I, "r1", "Resistor"));

    bp2::Blueprint::Nested n;
    n.id = I.intern("sub1");
    n.embedded = true;
    n.inline_def = std::make_unique<bp2::Blueprint>(inner);

    bp2::Blueprint root;
    root = root.with_node(make_node(I, "bat1", "Battery"));
    root = root.with_nested(std::move(n));

    Path bat = arena.make_node(arena.root(), I.intern("bat1"));
    Path bat_out = arena.make_port(bat, I.intern("v_out"));
    Path sub = arena.make_nested(arena.root(), I.intern("sub1"));
    Path r1 = arena.make_node(sub, I.intern("r1"));
    Path r1_in = arena.make_port(r1, I.intern("in"));

    PathResolver resolver;
    EXPECT_FALSE(resolver.can_connect(bat_out, r1_in, root, arena, reg));
}

TEST(PathResolver, CanConnectAcceptsSameScopeWithCompatibleDirections) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    Path bat = arena.make_node(arena.root(), I.intern("bat1"));
    Path res = arena.make_node(arena.root(), I.intern("res1"));
    Path src = arena.make_port(bat, I.intern("v_out"));
    Path tgt = arena.make_port(res, I.intern("in"));

    PathResolver resolver;
    EXPECT_TRUE(resolver.can_connect(src, tgt, bp, arena, reg));
}

TEST(WireValidator, ValidWirePasses) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("res1")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg);
    EXPECT_TRUE(r.valid) << r.error;
}

TEST(WireValidator, InvalidPathFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("ghost")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, DomainMismatchFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg;
    reg.register_component(
        I.intern("Src"),
        Interface({{I.intern("out"), Domain::Electrical, Direction::Output}})
    );
    reg.register_component(
        I.intern("Dst"),
        Interface({{I.intern("in"), Domain::Logical, Direction::Input}})
    );

    PathArena arena(I);
    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "a", "Src"));
    bp = bp.with_node(make_node(I, "b", "Dst"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("a")), I.intern("out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("b")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, DirectionMismatchFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "a", "Battery"));
    bp = bp.with_node(make_node(I, "b", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("a")), I.intern("v_in"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("b")), I.intern("v_in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, SelfLoopFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    Path p = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = p;
    w.target = p;
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg);
    EXPECT_FALSE(r.valid);
}

TEST(BlueprintValidate, DuplicateNodeIdsFail) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "dup", "Battery"));
    bp = bp.with_node(make_node(I, "dup", "Resistor"));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("dup"), std::string::npos);
    EXPECT_THROW(bp.validate(reg), std::runtime_error);
}

TEST(BlueprintValidate, DuplicateWireIdsFail) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    bp2::Blueprint::Wire w1;
    w1.id = I.intern("dup");
    w1.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w1.target = arena.make_port(arena.make_node(arena.root(), I.intern("res1")), I.intern("in"));
    w1.domain = Domain::Electrical;

    bp2::Blueprint::Wire w2 = w1;
    w2.target = arena.make_port(arena.make_node(arena.root(), I.intern("res1")), I.intern("out"));

    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("dup"), std::string::npos);
    EXPECT_THROW(bp.validate(reg), std::runtime_error);
}

TEST(BlueprintValidate, UnknownNodeTypeFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "n1", "NoSuchType"));

    PathArena arena(I);
    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("unknown node type"), std::string::npos);
    EXPECT_THROW(bp.validate(reg), std::runtime_error);
}

TEST(BlueprintValidate, InvalidNestedReferenceFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);

    bp2::Blueprint::Nested n;
    n.id = I.intern("sub1");
    n.embedded = false;
    n.blueprint_id = I.intern("NoSuchBlueprint");

    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(n));

    PathArena arena(I);
    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_FALSE(r.valid);
    EXPECT_THROW(bp.validate(reg), std::runtime_error);
}

TEST(BlueprintValidate, WirePathUnresolvedFails) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("ghost")), I.intern("in"));
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("wire id="), std::string::npos);
    EXPECT_THROW(bp.validate(reg, arena), std::runtime_error);
}

TEST(BlueprintValidate, ValidBlueprintPasses) {
    ui::StringInterner I;
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(I);
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("res1")), I.intern("in"));
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_NO_THROW(bp.validate(reg));
    EXPECT_NO_THROW(bp.validate(reg, arena));
}
