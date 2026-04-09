#include <gtest/gtest.h>

#include "blueprint_v2/path/path.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/diagnostics/repair.h"
#include "json_parser/json_parser.h"

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
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    return n;
}

static TypeRegistry make_validation_registry() {
    TypeRegistry reg = load_type_registry("library/");

    TypeDefinition battery;
    battery.classname = "Battery";
    battery.cpp_class = true;
    battery.ports["v_out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    battery.ports["v_in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    reg.types["Battery"] = std::move(battery);

    TypeDefinition resistor;
    resistor.classname = "Resistor";
    resistor.cpp_class = true;
    resistor.ports["in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    resistor.ports["out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    reg.types["Resistor"] = std::move(resistor);

    TypeDefinition composite;
    composite.classname = "CompositeType";
    composite.cpp_class = true;
    composite.ports["inner_only"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    reg.types["CompositeType"] = std::move(composite);

    return reg;
}

TEST(PathResolver, ResolveNodePortOnRoot) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    Path node = arena.make_node(arena.root(), I.intern("bat1"));
    Path port = arena.make_port(node, I.intern("v_out"));

    PathResolver resolver;
    auto rp = resolver.resolve(port, bp, arena, reg, I);
    ASSERT_TRUE(rp.has_value());
    EXPECT_EQ(rp->port.domain, Domain::Electrical);
    EXPECT_EQ(rp->port.direction, Direction::Output);
    EXPECT_FALSE(rp->is_boundary);
}

TEST(PathResolver, ResolveRootInterfacePort) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_interface(Interface({
        {I.intern("v_in"), Domain::Electrical, Direction::Input},
    }));

    Path iface = arena.make_port(arena.root(), I.intern("v_in"));
    PathResolver resolver;
    auto rp = resolver.resolve(iface, bp, arena, reg, I);
    ASSERT_TRUE(rp.has_value());
    EXPECT_TRUE(rp->is_boundary);
    EXPECT_EQ(rp->port.direction, Direction::Input);
}

TEST(PathResolver, ResolveNestedInterfacePort) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("in"), Domain::Electrical, Direction::Input},
        {I.intern("out"), Domain::Electrical, Direction::Output},
    }));

     auto n = bp2::Blueprint::Nested::make_embedded(
         I.intern("sub1"), ui::InternedId{},
         std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint root;
    root = root.with_nested(std::move(n));

    Path nested = arena.make_nested(arena.root(), I.intern("sub1"));
    Path iface = arena.make_port(nested, I.intern("in"));

    PathResolver resolver;
    auto rp = resolver.resolve(iface, root, arena, reg, I);
    ASSERT_TRUE(rp.has_value());
    EXPECT_TRUE(rp->is_boundary);
    EXPECT_EQ(rp->port.direction, Direction::Input);
}

TEST(PathResolver, CompositeHostNodePortUsesNestedAuthorityNotCollapsedCache) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("inner_only"), Domain::Electrical, Direction::Input},
    }));

    bp2::Blueprint::Node collapsed = make_node(I, "sub1", "CompositeType");
    collapsed.semantic.iface = Interface({
        {I.intern("stale_only"), Domain::Electrical, Direction::Input},
    });

    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub1"), I.intern("CompositeType"),
        std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint root;
    root = root.with_node(std::move(collapsed));
    root = root.with_nested(std::move(nested));

    Path node = arena.make_node(arena.root(), I.intern("sub1"));
    Path good_port = arena.make_port(node, I.intern("inner_only"));
    Path stale_port = arena.make_port(node, I.intern("stale_only"));

    PathResolver resolver;
    auto resolved_good = resolver.resolve(good_port, root, arena, reg, I);
    ASSERT_TRUE(resolved_good.has_value());
    EXPECT_EQ(resolved_good->port.name, I.intern("inner_only"));

    auto resolved_stale = resolver.resolve(stale_port, root, arena, reg, I);
    EXPECT_FALSE(resolved_stale.has_value());
}

TEST(PathResolver, CanConnectRejectsBoundarySkipAcrossNestedScopes) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
     inner = inner.with_node(make_node(I, "r1", "Resistor"));

     auto n = bp2::Blueprint::Nested::make_embedded(
         I.intern("sub1"), ui::InternedId{},
         std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint root;
    root = root.with_node(make_node(I, "bat1", "Battery"));
    root = root.with_nested(std::move(n));

    Path bat = arena.make_node(arena.root(), I.intern("bat1"));
    Path bat_out = arena.make_port(bat, I.intern("v_out"));
    Path sub = arena.make_nested(arena.root(), I.intern("sub1"));
    Path r1 = arena.make_node(sub, I.intern("r1"));
    Path r1_in = arena.make_port(r1, I.intern("in"));

    PathResolver resolver;
    EXPECT_FALSE(resolver.can_connect(bat_out, r1_in, root, arena, reg, I));
}

TEST(PathResolver, CanConnectAcceptsSameScopeWithCompatibleDirections) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    Path bat = arena.make_node(arena.root(), I.intern("bat1"));
    Path res = arena.make_node(arena.root(), I.intern("res1"));
    Path src = arena.make_port(bat, I.intern("v_out"));
    Path tgt = arena.make_port(res, I.intern("in"));

    PathResolver resolver;
    EXPECT_TRUE(resolver.can_connect(src, tgt, bp, arena, reg, I));
}

TEST(WireValidator, ValidWirePasses) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));
    bp = bp.with_node(make_node(I, "res1", "Resistor"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("res1")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
}

TEST(WireValidator, InvalidPathFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("ghost")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, DomainMismatchFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    TypeDefinition src;
    src.classname = "Src";
    src.cpp_class = true;
    src.ports["out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    reg.types["Src"] = std::move(src);
    TypeDefinition dst;
    dst.classname = "Dst";
    dst.cpp_class = true;
    dst.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["Dst"] = std::move(dst);

    PathArena arena(I);
    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "a", "Src"));
    bp = bp.with_node(make_node(I, "b", "Dst"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("a")), I.intern("out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("b")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, DirectionMismatchFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "a", "Battery"));
    bp = bp.with_node(make_node(I, "b", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("a")), I.intern("v_in"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("b")), I.intern("v_in"));
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, SelfLoopFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    Path p = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = p;
    w.target = p;
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
}

TEST(BlueprintValidate, DuplicateNodeIdsFail) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "dup", "Battery"));
    bp = bp.with_node(make_node(I, "dup", "Resistor"));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("dup"), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
}

TEST(BlueprintValidate, DuplicateWireIdsFail) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
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

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("dup"), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
}

TEST(BlueprintValidate, CompositeHostIfaceDesyncFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("inner_only"), Domain::Electrical, Direction::Input},
    }));

    bp2::Blueprint::Node collapsed = make_node(I, "sub1", "CompositeType");
    collapsed.view.expandable = true;
    collapsed.semantic.iface = Interface({
        {I.intern("stale_only"), Domain::Electrical, Direction::Input},
    });

    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub1"), I.intern("CompositeType"), std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint root;
    root = root.with_node(std::move(collapsed));
    root = root.with_nested(std::move(nested));

    auto r = bp2::InvariantChecker::validate(root, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("desynced"), std::string::npos);
}

TEST(BlueprintValidate, NestedInstanceMissingHostNodeFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("inner_only"), Domain::Electrical, Direction::Input},
    }));

    bp2::Blueprint root;
    root = root.with_nested(bp2::Blueprint::Nested::make_embedded(
        I.intern("sub1"), I.intern("CompositeType"), std::make_unique<bp2::Blueprint>(inner)));

    auto r = bp2::InvariantChecker::validate(root, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("missing host node"), std::string::npos);
}

TEST(BlueprintValidate, NestedHostNodeMustBeExpandable) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_interface(Interface({
        {I.intern("inner_only"), Domain::Electrical, Direction::Input},
    }));

    bp2::Blueprint::Node host = make_node(I, "sub1", "CompositeType");
    host.view.expandable = false;

    bp2::Blueprint root;
    root = root.with_node(std::move(host));
    root = root.with_nested(bp2::Blueprint::Nested::make_embedded(
        I.intern("sub1"), I.intern("CompositeType"), std::make_unique<bp2::Blueprint>(inner)));

    auto r = bp2::InvariantChecker::validate(root, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("must be expandable"), std::string::npos);
}

TEST(BlueprintValidate, NestedMissingHostBeatsIncidentalUnknownNodeTypeDiagnostic) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "ghost_unknown", "DefinitelyUnknownType"));

    auto inline_bp = std::make_unique<bp2::Blueprint>();
    *inline_bp = inline_bp->with_id(I.intern("RN-180-Exciter"));
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        I.intern("missing_host_inst"), I.intern("RN-180-Exciter"), std::move(inline_bp)));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("missing host node"), std::string::npos);
}

TEST(BlueprintValidate, NestedNonExpandableHostBeatsIncidentalUnknownNodeTypeDiagnostic) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "ghost_unknown", "DefinitelyUnknownType"));

    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("embedded_inst");
    host.semantic.type = I.intern("RN-180-Exciter");
    host.view.expandable = false;
    bp = bp.with_node(std::move(host));

    auto inline_bp = std::make_unique<bp2::Blueprint>();
    *inline_bp = inline_bp->with_id(I.intern("RN-180-Exciter"));
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        I.intern("embedded_inst"), I.intern("RN-180-Exciter"), std::move(inline_bp)));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("must be expandable"), std::string::npos);
}

TEST(BlueprintValidate, UnknownNodeTypeFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "n1", "NoSuchType"));

    PathArena arena(I);
    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("unknown node type"), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
}

TEST(BlueprintValidate, InvalidNestedReferenceFails) {
    ui::StringInterner I;
     TypeRegistry reg = make_validation_registry();

     auto n = bp2::Blueprint::Nested::make_reference(
         I.intern("sub1"), I.intern("NoSuchBlueprint"), bp2::Interface());

    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(n));

    PathArena arena(I);
    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
}

TEST(BlueprintValidate, WirePathUnresolvedFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("ghost")), I.intern("in"));
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("wire id="), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I, arena), std::runtime_error);
}

TEST(BlueprintValidate, ValidBlueprintPasses) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
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

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_NO_THROW(bp.validate(reg, I, arena));
}

TEST(BlueprintRepair, DiagnoseAndRepairRemovesInvalidWireEndpoints) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_bad");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat1")), I.intern("v_out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("ghost")), I.intern("in"));
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    auto report = bp2::diagnostics::diagnose_and_repair(bp, arena, reg, I);
    EXPECT_TRUE(report.changed);
    EXPECT_EQ(report.removed_wires, 1u);
    EXPECT_TRUE(bp.wires().empty());

    bool saw_invalid_wire_issue = false;
    for (const auto& issue : report.issues) {
        if (issue.kind == bp2::diagnostics::IntegrityIssue::Kind::InvalidWireEndpoint) {
            saw_invalid_wire_issue = true;
            break;
        }
    }
    EXPECT_TRUE(saw_invalid_wire_issue);
}

TEST(BlueprintRepair, DiagnoseReportsUnknownNodeType) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "n1", "NoSuchType"));

    auto report = bp2::diagnostics::diagnose_and_repair(bp, arena, reg, I);
    EXPECT_FALSE(report.changed);
    EXPECT_EQ(report.removed_wires, 0u);

    bool saw_unknown_type_issue = false;
    for (const auto& issue : report.issues) {
        if (issue.kind == bp2::diagnostics::IntegrityIssue::Kind::UnknownNodeType) {
            saw_unknown_type_issue = true;
            break;
        }
    }
    EXPECT_TRUE(saw_unknown_type_issue);
}

// ===========================================================================
// Regression: Embedded blueprint proxy nodes must be skipped during type checks.
//
// An embedded blueprint proxy has:  node.view.expandable =true, a matching nested
// entry with embedded=true, and a user-given type that is NOT in any registry.
// All validation paths must accept this pattern.
// ===========================================================================

TEST(BlueprintValidate, EmbeddedProxyNodePassesInvariantChecker) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    // Build a blueprint with an embedded proxy node whose type is unknown.
    bp2::Blueprint bp;
    bp2::Blueprint::Node proxy;
    proxy.semantic.id = I.intern("rn180_inst");
    proxy.semantic.type = I.intern("RN-180-Exciter");   // not in any registry
    proxy.view.expandable = true;
    bp = bp.with_node(std::move(proxy));

    // Add matching embedded nested definition.
    auto inline_bp = std::make_unique<bp2::Blueprint>();
    *inline_bp = inline_bp->with_id(I.intern("RN-180-Exciter"));
    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("rn180_inst"), I.intern("RN-180-Exciter"),
        std::move(inline_bp));
    bp = bp.with_nested(std::move(nested));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(r.valid) << "InvariantChecker rejected embedded proxy: " << r.error;
    EXPECT_NO_THROW(bp.validate(reg, I));
}

TEST(BlueprintValidate, NonEmbeddedExpandableNodeStillFailsTypeCheck) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    // An expandable node WITHOUT a matching embedded nested must still fail.
    bp2::Blueprint bp;
    bp2::Blueprint::Node proxy;
    proxy.semantic.id = I.intern("bad_proxy");
    proxy.semantic.type = I.intern("NonExistentType");
    proxy.view.expandable = true;
    bp = bp.with_node(std::move(proxy));

    // No nested entry at all.
    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("unknown node type"), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
}

TEST(BlueprintRepair, DiagnoseSkipsEmbeddedProxyNodes) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    bp2::Blueprint bp;
    bp2::Blueprint::Node proxy;
    proxy.semantic.id = I.intern("gen_inst");
    proxy.semantic.type = I.intern("GSC-18-Starter");
    proxy.view.expandable = true;
    bp = bp.with_node(std::move(proxy));

    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("gen_inst"), I.intern("GSC-18-Starter"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_nested(std::move(nested));

    auto report = bp2::diagnostics::diagnose_and_repair(bp, arena, reg, I);

    for (const auto& issue : report.issues) {
        EXPECT_NE(issue.kind, bp2::diagnostics::IntegrityIssue::Kind::UnknownNodeType)
            << "False positive: embedded proxy reported as unknown type: " << issue.message;
    }
}

TEST(BlueprintRepair, DiagnoseStillReportsNonProxyUnknownType) {
    ui::StringInterner I;
    TypeRegistry reg = load_type_registry("library/");
    PathArena arena(I);

    // An expandable node with no matching embedded nested → should still report.
    bp2::Blueprint bp;
    bp2::Blueprint::Node bad;
    bad.semantic.id = I.intern("orphan");
    bad.semantic.type = I.intern("MadeUpType");
    bad.view.expandable = true;
    bp = bp.with_node(std::move(bad));

    auto report = bp2::diagnostics::diagnose_and_repair(bp, arena, reg, I);
    bool found = false;
    for (const auto& issue : report.issues) {
        if (issue.kind == bp2::diagnostics::IntegrityIssue::Kind::UnknownNodeType) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(BlueprintValidate, ParserRegistryOverloadAcceptsKnownType) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");
    ASSERT_FALSE(parser_registry.types.empty());
    const std::string known_type = parser_registry.types.begin()->first;

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "n1", known_type.c_str()));

    EXPECT_NO_THROW(bp.validate(parser_registry, I));
    EXPECT_NO_THROW(bp.validate(parser_registry, I, arena));
}

TEST(BlueprintRepair, ParserRegistryOverloadSkipsEmbeddedProxyNodes) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node proxy;
    proxy.semantic.id = I.intern("gen_inst");
    proxy.semantic.type = I.intern("GSC-18-Starter");
    proxy.view.expandable = true;
    bp = bp.with_node(std::move(proxy));

    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("gen_inst"), I.intern("GSC-18-Starter"),
        std::make_unique<bp2::Blueprint>());
    bp = bp.with_nested(std::move(nested));

    auto report = bp2::diagnostics::diagnose_and_repair(bp, arena, parser_registry, I);

    for (const auto& issue : report.issues) {
        EXPECT_NE(issue.kind, bp2::diagnostics::IntegrityIssue::Kind::UnknownNodeType)
            << "False positive: embedded proxy reported as unknown type: " << issue.message;
    }
}

TEST(PathResolver, ParserRegistryOverloadResolveUsesCanonicalRegistryInput) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern("n1");
    n.semantic.type = I.intern("AnyType");
    n.semantic.iface = bp2::Interface({
        {I.intern("p"), Domain::Electrical, bp2::Direction::Output},
    });
    bp = bp.with_node(std::move(n));

    bp2::Path node = arena.make_node(arena.root(), I.intern("n1"));
    bp2::Path port = arena.make_port(node, I.intern("p"));

    bp2::PathResolver resolver;
    auto resolved = resolver.resolve(port, bp, arena, parser_registry, I);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->port.name, I.intern("p"));
}

TEST(WireValidator, ParserRegistryOverloadValidateWire) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");
    ASSERT_FALSE(parser_registry.types.empty());
    const std::string known_type = parser_registry.types.begin()->first;

    bp2::Blueprint bp;
    bp2::Blueprint::Node a;
    a.semantic.id = I.intern("a");
    a.semantic.type = I.intern(known_type);
    a.semantic.iface = bp2::Interface({
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    });
    bp = bp.with_node(std::move(a));

    bp2::Blueprint::Node b;
    b.semantic.id = I.intern("b");
    b.semantic.type = I.intern(known_type);
    b.semantic.iface = bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
    });
    bp = bp.with_node(std::move(b));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern("a")), I.intern("out"));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern("b")), I.intern("in"));
    w.domain = Domain::Electrical;

    auto vr = bp2::WireValidator::validate(w, bp, arena, parser_registry, I);
    EXPECT_TRUE(vr.valid) << vr.error;
}

TEST(InvariantChecker, ParserRegistryOverloadValidateBlueprint) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");
    ASSERT_FALSE(parser_registry.types.empty());
    const std::string known_type = parser_registry.types.begin()->first;

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "n1", known_type.c_str()));

    auto result = bp2::InvariantChecker::validate(bp, arena, parser_registry, I);
    EXPECT_TRUE(result.valid) << result.error;
}

TEST(InvariantChecker, ReferencedNestedHostPathMismatchDetected) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();

    // Add a reference-mode nested
    auto nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("CompositeType"),
        bp2::Interface{});
    
    // Create host node with mismatched blueprint_path
    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("ref1");
    host.semantic.type = I.intern("CompositeType");
    host.view.expandable = true;
    host.view.blueprint_path = "wrong/path";  // This doesn't match CompositeType category
    
    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(host));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(result.error.find("blueprint_path mismatch") != std::string::npos)
        << "Error message: " << result.error;
}

TEST(InvariantChecker, ReferencedNestedCategorizedHostPathMismatchDetected) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();
    reg.categories["CompositeType"] = "math";

    auto nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("CompositeType"),
        bp2::Interface{});

    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("ref1");
    host.semantic.type = I.intern("CompositeType");
    host.view.expandable = true;
    host.view.blueprint_path = "CompositeType";

    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(host));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(result.error.find("expected=math/CompositeType") != std::string::npos)
        << "Error message: " << result.error;
}

TEST(InvariantChecker, ReferencedNestedWithCorrectHostPathPassesValidation) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();

    // Add a reference-mode nested
    auto nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("CompositeType"),
        bp2::Interface{});
    
    // Create host node with matching blueprint_path (no category in this case)
    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("ref1");
    host.semantic.type = I.intern("CompositeType");
    host.view.expandable = true;
    host.view.blueprint_path = "CompositeType";  // Matches blueprint_id with no category
    
    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(host));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(result.valid) << result.error;
}

TEST(InvariantChecker, ReferencedNestedWithCategorizedHostPathPassesValidation) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();
    reg.categories["CompositeType"] = "math";

    auto nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("CompositeType"),
        bp2::Interface{});

    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("ref1");
    host.semantic.type = I.intern("CompositeType");
    host.view.expandable = true;
    host.view.blueprint_path = "math/CompositeType";

    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(host));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(result.valid) << result.error;
}

TEST(InvariantChecker, ReferencedNestedWithEmptyHostPathAllowedUnderCurrentDesign) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();

    // Add a reference-mode nested
    auto nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("CompositeType"),
        bp2::Interface{});
    
    // Create host node with empty blueprint_path
    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("ref1");
    host.semantic.type = I.intern("CompositeType");
    host.view.expandable = true;
    host.view.blueprint_path = "";  // Empty path is allowed
    
    bp2::Blueprint bp;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(host));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(result.valid) << result.error;
}
