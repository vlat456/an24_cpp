#include <gtest/gtest.h>

#include "blueprint_v2/path/path.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/blueprint/canonicalize.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/port_compatibility.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/validation/signal_typing.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/diagnostics/repair.h"
#include "json_parser/json_parser.h"

#include "../bp2_test_helpers.h"

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
    // Note: Interface is NOT populated here - tests must populate if needed
    // or use a registry-aware make_node variant
    return n;
}

/// Create a node with interface populated from registry
static bp2::Blueprint::Node make_node_with_interface(ui::StringInterner& I,
                                                     const char* id,
                                                     const char* type,
                                                     const TypeRegistry& reg) {
    bp2::Blueprint::Node n = make_node(I, id, type);
    const std::string type_str(type);
    const auto* def = reg.get(type_str);
    if (def) {
        n.content = bp2::Blueprint::Node::ComponentData{
            bp2::interface_from_type_definition(*def, I)
        };
    }
    return n;
}

static bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                             const char* exposed_port,
                                             bp2::Blueprint::Node::BridgePortSide side,
                                             PortType port_type) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(exposed_port);
    n.semantic.type = I.intern("BridgePort");

    const bool is_input = side == bp2::Blueprint::Node::BridgePortSide::Input;
    n.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(exposed_port),
        side,
        port_type,
        bp2::Interface({
            {I.intern("ext"), domain_for_port_type(port_type), is_input ? Direction::Input : Direction::Output, port_type},
            {I.intern("port"), domain_for_port_type(port_type), is_input ? Direction::Output : Direction::Input, port_type},
        })
    };
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
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("res1"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
}

TEST(PortCompatibility, LegacyDomainResolutionCases) {
    ui::StringInterner I;
    const PortDescriptor electrical_out{I.intern("out"), Domain::Electrical, Direction::Output, PortType::V};
    const PortDescriptor electrical_any{I.intern("any_e"), Domain::Electrical, Direction::Input, PortType::Any};
    const PortDescriptor logical_any{I.intern("any_l"), Domain::Logical, Direction::Input, PortType::Any};
    const PortDescriptor logical_bool{I.intern("bool"), Domain::Logical, Direction::Input, PortType::Bool};

    auto exact = bp2::resolve_port_domain(electrical_out, PortDescriptor{I.intern("in"), Domain::Electrical, Direction::Input, PortType::V});
    EXPECT_EQ(exact.kind, bp2::PortDomainResolutionKind::ExactMatch);
    ASSERT_TRUE(exact.domain.has_value());
    EXPECT_EQ(*exact.domain, Domain::Electrical);

    auto src_any = bp2::resolve_port_domain(electrical_any, logical_bool);
    EXPECT_EQ(src_any.kind, bp2::PortDomainResolutionKind::SourceAnyAdoptsTarget);
    ASSERT_TRUE(src_any.domain.has_value());
    EXPECT_EQ(*src_any.domain, Domain::Logical);

    auto tgt_any = bp2::resolve_port_domain(electrical_out, logical_any);
    EXPECT_EQ(tgt_any.kind, bp2::PortDomainResolutionKind::TargetAnyAdoptsSource);
    ASSERT_TRUE(tgt_any.domain.has_value());
    EXPECT_EQ(*tgt_any.domain, Domain::Electrical);

    auto both_any = bp2::resolve_port_domain(electrical_any, logical_any);
    EXPECT_EQ(both_any.kind, bp2::PortDomainResolutionKind::BothAnyAmbiguous);
    ASSERT_TRUE(both_any.domain.has_value());
    EXPECT_EQ(*both_any.domain, Domain::Electrical);
    EXPECT_TRUE(both_any.ambiguous());

    auto mismatch = bp2::resolve_port_domain(electrical_out, logical_bool);
    EXPECT_EQ(mismatch.kind, bp2::PortDomainResolutionKind::Mismatch);
    EXPECT_FALSE(mismatch.domain.has_value());
}

TEST(WireValidator, AnyToAnyUsesSharedLegacyResolutionRule) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition src;
    src.classname = "SrcAny";
    src.cpp_class = true;
    src.ports["out"] = Port{PortDirection::Out, PortType::Any, Domain::Electrical, false};
    reg.types["SrcAny"] = std::move(src);

    TypeDefinition dst;
    dst.classname = "DstAny";
    dst.cpp_class = true;
    dst.ports["in"] = Port{PortDirection::In, PortType::Any, Domain::Logical, false};
    reg.types["DstAny"] = std::move(dst);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "src", "SrcAny"));
    bp = bp.with_node(make_node(I, "dst", "DstAny"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_any_any");
    w.source = bp2::WireEndpoint{I.intern("src"), I.intern("out")};
    w.target = bp2::WireEndpoint{I.intern("dst"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.resolved_domain, Domain::Electrical);
}

TEST(WireValidator, ContextualBindsToConcreteAnchor) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition value;
    value.classname = "Value";
    value.cpp_class = true;
    value.ports["o"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false};
    reg.types["Value"] = std::move(value);

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    sink.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["BoolSink"] = std::move(sink);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "value", "Value"));
    bp = bp.with_node(make_node(I, "sink", "BoolSink"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_ctx_bool");
    w.source = bp2::WireEndpoint{I.intern("value"), I.intern("o")};
    w.target = bp2::WireEndpoint{I.intern("sink"), I.intern("in")};
    w.domain = Domain::Logical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.resolved_domain, Domain::Logical);
}

TEST(WireValidator, ContextualOnlySignalFailsExplicitly) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition lhs;
    lhs.classname = "CtxOut";
    lhs.cpp_class = true;
    lhs.ports["out"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false};
    reg.types["CtxOut"] = std::move(lhs);

    TypeDefinition rhs;
    rhs.classname = "CtxIn";
    rhs.cpp_class = true;
    rhs.ports["in"] = Port{PortDirection::In, PortType::Contextual, Domain::Electrical, false};
    reg.types["CtxIn"] = std::move(rhs);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "a", "CtxOut"));
    bp = bp.with_node(make_node(I, "b", "CtxIn"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_ctx_ctx");
    w.source = bp2::WireEndpoint{I.intern("a"), I.intern("out")};
    w.target = bp2::WireEndpoint{I.intern("b"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error, "wire signal typing unresolved");
}

TEST(WireValidator, ContextualBridgeBindsToExposedRootPort) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    sink.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["BoolSink"] = std::move(sink);

    bp2::Blueprint bp;
    bp = bp.with_interface(Interface({
        {I.intern("flag"), Domain::Logical, Direction::Input, PortType::Bool},
    }));

    bp = bp.with_node(make_bridge_node(I, "flag", bp2::Blueprint::Node::BridgePortSide::Input, PortType::Contextual));
    bp = bp.with_node(make_node(I, "sink", "BoolSink"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_bridge_bool");
    w.source = bp2::WireEndpoint{I.intern("flag"), I.intern("port")};
    w.target = bp2::WireEndpoint{I.intern("sink"), I.intern("in")};
    w.domain = Domain::Logical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.resolved_domain, Domain::Logical);
}

TEST(WireValidator, ContextualAliasGroupBindsTransitivelyToConcreteAnchor) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition splitter;
    splitter.classname = "CtxSplitter";
    splitter.cpp_class = true;
    splitter.ports["i"] = Port{PortDirection::In, PortType::Contextual, Domain::Electrical, false};
    splitter.ports["o1"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false, std::string("i")};
    splitter.ports["o2"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false, std::string("i")};
    reg.types["CtxSplitter"] = std::move(splitter);

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    sink.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["BoolSink"] = std::move(sink);

    TypeDefinition value;
    value.classname = "Value";
    value.cpp_class = true;
    value.ports["o"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false};
    reg.types["Value"] = std::move(value);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "value", "Value"));
    bp = bp.with_node(make_node(I, "split", "CtxSplitter"));
    bp = bp.with_node(make_node(I, "sink", "BoolSink"));

    bp2::Blueprint::Wire w1;
    w1.id = I.intern("w_ctx_alias_1");
    w1.source = bp2::WireEndpoint{I.intern("value"), I.intern("o")};
    w1.target = bp2::WireEndpoint{I.intern("split"), I.intern("i")};
    w1.domain = Domain::Logical;
    bp = bp.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = I.intern("w_ctx_alias_2");
    w2.source = bp2::WireEndpoint{I.intern("split"), I.intern("o2")};
    w2.target = bp2::WireEndpoint{I.intern("sink"), I.intern("in")};
    w2.domain = Domain::Logical;

    auto r = WireValidator::validate(w2, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.resolved_domain, Domain::Logical);
}

TEST(WireValidator, ContextualAndAnyWithoutConcreteAnchorFailsExplicitly) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition src;
    src.classname = "CtxOut";
    src.cpp_class = true;
    src.ports["out"] = Port{PortDirection::Out, PortType::Contextual, Domain::Electrical, false};
    reg.types["CtxOut"] = std::move(src);

    TypeDefinition dst;
    dst.classname = "AnyIn";
    dst.cpp_class = true;
    dst.ports["in"] = Port{PortDirection::In, PortType::Any, Domain::Logical, false};
    reg.types["AnyIn"] = std::move(dst);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "ctx", "CtxOut"));
    bp = bp.with_node(make_node(I, "any", "AnyIn"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_ctx_any");
    w.source = bp2::WireEndpoint{I.intern("ctx"), I.intern("out")};
    w.target = bp2::WireEndpoint{I.intern("any"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error, "wire signal typing unresolved");
}

TEST(WireValidator, SignalValueBindsToSignalMathPort) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition src;
    src.classname = "Value";
    src.cpp_class = true;
    src.ports["o"] = Port{PortDirection::Out, PortType::Signal, Domain::Logical, false};
    reg.types["Value"] = std::move(src);

    TypeDefinition dst;
    dst.classname = "Multiply";
    dst.cpp_class = true;
    dst.ports["A"] = Port{PortDirection::In, PortType::Signal, Domain::Logical, false};
    dst.ports["B"] = Port{PortDirection::In, PortType::Signal, Domain::Logical, false};
    dst.ports["o"] = Port{PortDirection::Out, PortType::Signal, Domain::Logical, false};
    reg.types["Multiply"] = std::move(dst);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "value", "Value"));
    bp = bp.with_node(make_node(I, "mul", "Multiply"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_ctx_any_math");
    w.source = bp2::WireEndpoint{I.intern("value"), I.intern("o")};
    w.target = bp2::WireEndpoint{I.intern("mul"), I.intern("B")};
    w.domain = Domain::Logical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.resolved_domain, Domain::Logical);
}

TEST(WireValidator, BridgeWithoutMatchingExposedRootPortFailsExplicitly) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    sink.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["BoolSink"] = std::move(sink);

    bp2::Blueprint bp;
    bp = bp.with_interface(Interface({
        {I.intern("other_flag"), Domain::Logical, Direction::Input, PortType::Bool},
    }));

    bp = bp.with_node(make_bridge_node(I, "flag", bp2::Blueprint::Node::BridgePortSide::Input, PortType::Contextual));
    bp = bp.with_node(make_node(I, "sink", "BoolSink"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w_bad_bridge");
    w.source = bp2::WireEndpoint{I.intern("flag"), I.intern("port")};
    w.target = bp2::WireEndpoint{I.intern("sink"), I.intern("in")};
    w.domain = Domain::Logical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error, "wire signal typing unresolved");
}

TEST(WireValidator, NestedEmbeddedContextualBridgeChainBindsToRootConcreteAnchor) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    sink.ports["in"] = Port{PortDirection::In, PortType::Bool, Domain::Logical, false};
    reg.types["BoolSink"] = std::move(sink);

    bp2::Blueprint leaf;
    leaf = leaf.with_id(I.intern("LeafType"));
    leaf = leaf.with_name("LeafType");
    leaf = leaf.with_interface(Interface({
        {I.intern("flag"), Domain::Logical, Direction::Input, PortType::Bool},
    }));

    leaf = leaf.with_node(make_bridge_node(I, "flag", bp2::Blueprint::Node::BridgePortSide::Input, PortType::Contextual));
    leaf = leaf.with_node(make_node(I, "sink", "BoolSink"));

    bp2::Blueprint::Wire leaf_wire;
    leaf_wire.id = I.intern("leaf_wire");
    leaf_wire.source = bp2::WireEndpoint{I.intern("flag"), I.intern("port")};
    leaf_wire.target = bp2::WireEndpoint{I.intern("sink"), I.intern("in")};
    leaf_wire.domain = Domain::Logical;
    leaf = leaf.with_wire(std::move(leaf_wire));

    bp2::Blueprint mid;
    mid = mid.with_id(I.intern("MidType"));
    mid = mid.with_name("MidType");
    mid = mid.with_interface(Interface({
        {I.intern("flag"), Domain::Logical, Direction::Input, PortType::Bool},
    }));

    mid = mid.with_node(make_bridge_node(I, "flag", bp2::Blueprint::Node::BridgePortSide::Input, PortType::Contextual));

    bp2::Blueprint::Node leaf_inst;
    leaf_inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            I.intern("LeafType"),
            std::make_unique<bp2::Blueprint>(leaf))
    };
    leaf_inst.semantic.id = I.intern("leaf");
    leaf_inst.semantic.type = I.intern("LeafType");
    mid = mid.with_node(std::move(leaf_inst));

    bp2::Blueprint::Wire mid_wire;
    mid_wire.id = I.intern("mid_wire");
    mid_wire.source = bp2::WireEndpoint{I.intern("flag"), I.intern("port")};
    mid_wire.target = bp2::WireEndpoint{I.intern("leaf"), I.intern("flag")};
    mid_wire.domain = Domain::Logical;
    mid = mid.with_wire(std::move(mid_wire));

    auto mid_resolved = bp2::resolve_signal_typing(
        mid,
        &reg,
        I,
        bp2::WireEndpoint{I.intern("flag"), I.intern("port")},
        bp2::WireEndpoint{I.intern("leaf"), I.intern("flag")});
    ASSERT_TRUE(mid_resolved.resolved.has_value());
    EXPECT_EQ(mid_resolved.resolved->domain, Domain::Logical);
    EXPECT_EQ(mid_resolved.resolved->port_type, PortType::Bool);

    auto leaf_resolved = bp2::resolve_signal_typing(
        leaf,
        &reg,
        I,
        bp2::WireEndpoint{I.intern("flag"), I.intern("port")},
        bp2::WireEndpoint{I.intern("sink"), I.intern("in")});
    ASSERT_TRUE(leaf_resolved.resolved.has_value());
    EXPECT_EQ(leaf_resolved.resolved->domain, Domain::Logical);
    EXPECT_EQ(leaf_resolved.resolved->port_type, PortType::Bool);
}

TEST(WireValidator, InvalidPathFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("ghost"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
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
    w.source = bp2::WireEndpoint{I.intern("a"), I.intern("out")};
    w.target = bp2::WireEndpoint{I.intern("b"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
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
    w.source = bp2::WireEndpoint{I.intern("a"), I.intern("v_in")};
    w.target = bp2::WireEndpoint{I.intern("b"), I.intern("v_in")};
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
    EXPECT_FALSE(r.valid);
}

TEST(WireValidator, SelfLoopFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));

    bp2::WireEndpoint p{I.intern("bat1"), I.intern("v_out")};

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = p;
    w.target = p;
    w.domain = Domain::Electrical;

    auto r = WireValidator::validate(w, bp, reg, I);
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
    w1.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w1.target = bp2::WireEndpoint{I.intern("res1"), I.intern("in")};
    w1.domain = Domain::Electrical;

    bp2::Blueprint::Wire w2 = w1;
    w2.target = bp2::WireEndpoint{I.intern("res1"), I.intern("out")};

    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));

    auto r = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error.find("dup"), std::string::npos);
    EXPECT_THROW(bp.validate(reg, I), std::runtime_error);
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







TEST(InvariantChecker, RootComponentNodePasses) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry reg = make_validation_registry();

    bp2::Blueprint bp;
    bp = bp.with_node(make_node_with_interface(I, "child1", "Battery", reg));

    auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
    EXPECT_TRUE(result.valid) << result.error;
}



TEST(BlueprintValidate, WirePathUnresolvedFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);

    bp2::Blueprint bp;
    bp = bp.with_node(make_node_with_interface(I, "bat1", "Battery", reg));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("ghost"), I.intern("in")};
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
    bp = bp.with_node(make_node_with_interface(I, "bat1", "Battery", reg));
    bp = bp.with_node(make_node_with_interface(I, "res1", "Resistor", reg));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("res1"), I.intern("in")};
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
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("ghost"), I.intern("in")};
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





TEST(BlueprintValidate, ParserRegistryOverloadAcceptsKnownType) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");
    ASSERT_FALSE(parser_registry.types.empty());
    const std::string known_type = parser_registry.types.begin()->first;

    bp2::Blueprint bp;
    bp = bp.with_node(make_node_with_interface(I, "n1", known_type.c_str(), parser_registry));

    EXPECT_NO_THROW(bp.validate(parser_registry, I));
    EXPECT_NO_THROW(bp.validate(parser_registry, I, arena));
}


TEST(PathResolver, ParserRegistryOverloadResolveUsesCanonicalRegistryInput) {
    ui::StringInterner I;
    PathArena arena(I);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern("n1");
    n.semantic.type = I.intern("AnyType");
    n.content = bp2::Blueprint::Node::ComponentData{bp2::Interface({
        {I.intern("p"), Domain::Electrical, bp2::Direction::Output},
    })};
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
    a.content = bp2::Blueprint::Node::ComponentData{bp2::Interface({
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    })};
    bp = bp.with_node(std::move(a));

    bp2::Blueprint::Node b;
    b.semantic.id = I.intern("b");
    b.semantic.type = I.intern(known_type);
    b.content = bp2::Blueprint::Node::ComponentData{bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
    })};
    bp = bp.with_node(std::move(b));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("a"), I.intern("out")};
    w.target = bp2::WireEndpoint{I.intern("b"), I.intern("in")};
    w.domain = Domain::Electrical;

    auto vr = bp2::WireValidator::validate(w, bp, parser_registry, I);
    EXPECT_TRUE(vr.valid) << vr.error;
}

TEST(InvariantChecker, ParserRegistryOverloadValidateBlueprint) {
    ui::StringInterner I;
     PathArena arena(I);
     TypeRegistry parser_registry = load_type_registry("library/");
     ASSERT_FALSE(parser_registry.types.empty());
     const std::string known_type = parser_registry.types.begin()->first;
 
     bp2::Blueprint bp;
     bp = bp.with_node(make_node_with_interface(I, "n1", known_type.c_str(), parser_registry));
 
     auto result = bp2::InvariantChecker::validate(bp, arena, parser_registry, I);
     EXPECT_TRUE(result.valid) << result.error;
}

/// Wire declared as Electrical but ports are actually compatible and
/// wire should still fail strict validation because declared domain !=
/// resolved domain (Gap #3: Wire Domain Consistency)
TEST(WireValidator, WireDomainDeclaredMismatchesPorts) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat1", "Battery"));  // ports are Electrical
    bp = bp.with_node(make_node(I, "res1", "Resistor")); // ports are Electrical

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("res1"), I.intern("in")};
    w.domain = Domain::Logical;  // <-- Declared as Logical but ports are Electrical

     auto r = WireValidator::validate(w, bp, reg, I);
     EXPECT_FALSE(r.valid);
     EXPECT_NE(r.error.find("domain"), std::string::npos);
}

/// Issue #88 Gap #2: Required parameters must be present in decoded blueprints
TEST(BlueprintDecode, RequiredParamValidation_MissingRequiredParamFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    
    // Add a component with a required param
    TypeDefinition test_type;
    test_type.classname = "TestComponent";
    test_type.cpp_class = true;
    test_type.ports["in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    test_type.ports["out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    ParamSchemaEntry req_param;
    req_param.type = ParamSchemaType::Float;
    req_param.required = true;
    test_type.param_schema["critical_value"] = req_param;
    reg.types["TestComponent"] = std::move(test_type);
    
    // Try to decode a blueprint with TestComponent but missing the required param
    std::string json_str = R"({
      "format": "blueprint",
      "version": 1,
      "blueprint_id": "test_missing_required",
      "name": "Test Missing Required",
      "interface": [],
      "nodes": [
        {
          "id": "tc1",
          "kind": "component",
          "component": "TestComponent",
          "layout": {"x": 0.0, "y": 0.0}
        }
      ],
      "wires": []
    })";
    
    bp2::PathArena arena(I);
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(json_str, I, arena, reg, &err);
    
    // Should fail because critical_value param is required but missing
    EXPECT_FALSE(bp.has_value());
    EXPECT_NE(err.message.find("critical_value"), std::string::npos);
    EXPECT_NE(err.message.find("required"), std::string::npos);
}

/// Issue #88 Gap #2: Required parameters are accepted when present
TEST(BlueprintDecode, RequiredParamValidation_PresentRequiredParamPasses) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    
    // Add a component with a required param
    TypeDefinition test_type;
    test_type.classname = "TestComponent";
    test_type.cpp_class = true;
    test_type.ports["in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    test_type.ports["out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    ParamSchemaEntry req_param;
    req_param.type = ParamSchemaType::Float;
    req_param.required = true;
    test_type.param_schema["critical_value"] = req_param;
    reg.types["TestComponent"] = std::move(test_type);
    
    // Decode a blueprint with TestComponent AND the required param present
    std::string json_str = R"({
      "format": "blueprint",
      "version": 1,
      "blueprint_id": "test_has_required",
      "name": "Test Has Required",
      "interface": [],
      "nodes": [
        {
          "id": "tc1",
          "kind": "component",
          "component": "TestComponent",
          "params": {"critical_value": 42.0},
          "layout": {"x": 0.0, "y": 0.0}
        }
      ],
      "wires": []
    })";
    
    bp2::PathArena arena(I);
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(json_str, I, arena, reg, &err);
    
    // Should succeed because critical_value is provided
    EXPECT_TRUE(bp.has_value()) << "Decode failed: " << err.message;
}

/// Issue #88 Gap #2: Optional params are not required
TEST(BlueprintDecode, RequiredParamValidation_OptionalParamCanBeMissing) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    
    // Add a component with an optional param (required=false)
    TypeDefinition test_type;
    test_type.classname = "TestComponent";
    test_type.cpp_class = true;
    test_type.ports["in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    test_type.ports["out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    ParamSchemaEntry opt_param;
    opt_param.type = ParamSchemaType::Float;
    opt_param.required = false;  // Optional
    test_type.param_schema["optional_value"] = opt_param;
    reg.types["TestComponent"] = std::move(test_type);
    
    // Decode a blueprint with TestComponent but NO optional param
    std::string json_str = R"({
      "format": "blueprint",
      "version": 1,
      "blueprint_id": "test_optional_missing",
      "name": "Test Optional Missing",
      "interface": [],
      "nodes": [
        {
          "id": "tc1",
          "kind": "component",
          "component": "TestComponent",
          "layout": {"x": 0.0, "y": 0.0}
        }
      ],
      "wires": []
    })";
    
    bp2::PathArena arena(I);
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(json_str, I, arena, reg, &err);
    
    // Should succeed because optional_value is optional
    EXPECT_TRUE(bp.has_value()) << "Decode failed: " << err.message;
}

/// Issue #88 Gap #4: Embedded blueprints with invalid internal wires should fail validation
TEST(InvariantChecker, RecursiveValidation_EmbeddedWithSelfLoopFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);
    
    // Create an embedded blueprint that contains a self-loop wire (invalid)
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_node(make_node_with_interface(I, "bat1", "Battery", reg));
    
    bp2::Blueprint::Wire bad_wire;
    bad_wire.id = I.intern("self_loop");
    bad_wire.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    bad_wire.target = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    bad_wire.domain = Domain::Electrical;
    inner_bp = inner_bp.with_wire(std::move(bad_wire));
    
    // Create parent blueprint with embedded instance
    bp2::Blueprint parent_bp;
    bp2::Blueprint::Node composite_node;
    composite_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            I.intern("CompositeType"),
            std::make_unique<bp2::Blueprint>(inner_bp)
        )
    };
    composite_node.semantic.id = I.intern("composite1");
    composite_node.semantic.type = I.intern("Battery");  // Use valid type from registry
    parent_bp = parent_bp.with_node(std::move(composite_node));
    
    // Validate parent blueprint - should fail because embedded has invalid self-loop
    auto result = bp2::InvariantChecker::validate(parent_bp, arena, reg, I);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("wire"), std::string::npos);  // Error will mention wire issue
}

/// Issue #88 Gap #4: Embedded blueprints with duplicate node IDs should fail validation
TEST(InvariantChecker, RecursiveValidation_EmbeddedWithDuplicateNodesFails) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);
    
    // Create an embedded blueprint that contains duplicate node IDs (invalid)
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_node(make_node_with_interface(I, "dup", "Battery", reg));
    inner_bp = inner_bp.with_node(make_node_with_interface(I, "dup", "Resistor", reg));
    
    // Create parent blueprint with embedded instance
    bp2::Blueprint parent_bp;
    bp2::Blueprint::Node composite_node;
    composite_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            I.intern("CompositeType"),
            std::make_unique<bp2::Blueprint>(inner_bp)
        )
    };
    composite_node.semantic.id = I.intern("composite1");
    composite_node.semantic.type = I.intern("Battery");  // Use valid type from registry
    parent_bp = parent_bp.with_node(std::move(composite_node));
    
    // Validate parent blueprint - should fail because embedded has duplicate node IDs
    auto result = bp2::InvariantChecker::validate(parent_bp, arena, reg, I);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("duplicate"), std::string::npos);
}

/// Issue #88 Gap #4: Valid embedded blueprints pass recursive validation
TEST(InvariantChecker, RecursiveValidation_ValidEmbeddedPasses) {
    ui::StringInterner I;
    TypeRegistry reg = make_validation_registry();
    PathArena arena(I);
    
    // Create a valid embedded blueprint
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_node(make_node_with_interface(I, "bat1", "Battery", reg));
    inner_bp = inner_bp.with_node(make_node_with_interface(I, "res1", "Resistor", reg));
    
    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("bat1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("res1"), I.intern("in")};
    w.domain = Domain::Electrical;
    inner_bp = inner_bp.with_wire(std::move(w));
    
    // Create parent blueprint with valid embedded instance
    bp2::Blueprint parent_bp;
    bp2::Blueprint::Node composite_node;
    composite_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            I.intern("CompositeType"),
            std::make_unique<bp2::Blueprint>(inner_bp)
        )
    };
    composite_node.semantic.id = I.intern("composite1");
    composite_node.semantic.type = I.intern("Battery");  // Use valid type from registry
     parent_bp = parent_bp.with_node(std::move(composite_node));
     
     // Validate parent blueprint - should pass
     auto result = bp2::InvariantChecker::validate(parent_bp, arena, reg, I);
     EXPECT_TRUE(result.valid) << result.error;
}

/// Issue #88 Gap #5: Component nodes must have interface consistency with registry
TEST(InvariantChecker, ComponentNodeInterfaceConsistency_ValidComponentPasses) {
     ui::StringInterner I;
     TypeRegistry reg = make_validation_registry();
     PathArena arena(I);
     
     // Create a Battery component with correct interface from registry
     bp2::Blueprint bp;
     bp2::Blueprint::Node bat_node = make_node(I, "bat1", "Battery");
     
     // Get the actual interface Battery should have
     const TypeDefinition* battery_def = reg.get("Battery");
     ASSERT_TRUE(battery_def);
     bat_node.content = bp2::Blueprint::Node::ComponentData{
         bp2::interface_from_type_definition(*battery_def, I)
     };
     
     bp = bp.with_node(std::move(bat_node));
     
     // Validate blueprint - should pass
     auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
     EXPECT_TRUE(result.valid) << result.error;
}

/// Issue #88 Gap #5: Component node with mismatched interface should fail
TEST(InvariantChecker, ComponentNodeInterfaceConsistency_InterfaceMismatchFails) {
     ui::StringInterner I;
     TypeRegistry reg = make_validation_registry();
     PathArena arena(I);
     
     // Create a Battery component but with WRONG interface (Resistor's ports)
     bp2::Blueprint bp;
     bp2::Blueprint::Node bat_node = make_node(I, "bat1", "Battery");
     
      // Set interface to wrong type (Resistor ports instead of Battery ports)
      bat_node.content = bp2::Blueprint::Node::ComponentData{bp2::Interface(std::vector<PortDescriptor>{
          {I.intern("in"), Domain::Electrical, Direction::Input},
          {I.intern("out"), Domain::Electrical, Direction::Output},
      })};
     
     bp = bp.with_node(std::move(bat_node));
     
     // Validate blueprint - should fail with iface desynced error
     auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
     EXPECT_FALSE(result.valid);
     EXPECT_NE(result.error.find("iface desynced"), std::string::npos);
}

/// Issue #88 Gap #5: Component node with empty interface when ports exist should fail
TEST(InvariantChecker, ComponentNodeInterfaceConsistency_EmptyInterfaceOnValidComponentFails) {
     ui::StringInterner I;
     TypeRegistry reg = make_validation_registry();
     PathArena arena(I);
     
     // Create a Battery component but with EMPTY interface (should have v_out, v_in)
     bp2::Blueprint bp;
     bp2::Blueprint::Node bat_node = make_node(I, "bat1", "Battery");
     
      // Leave interface empty - Battery has ports so this is invalid
      bat_node.content = bp2::Blueprint::Node::ComponentData{bp2::Interface(std::vector<PortDescriptor>{})};
     
     bp = bp.with_node(std::move(bat_node));
     
     // Validate blueprint - should fail with iface desynced error
     auto result = bp2::InvariantChecker::validate(bp, arena, reg, I);
     EXPECT_FALSE(result.valid);
     EXPECT_NE(result.error.find("iface desynced"), std::string::npos);
}
