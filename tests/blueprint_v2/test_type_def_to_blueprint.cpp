#include <gtest/gtest.h>

#include "blueprint_v2/library/type_def_to_blueprint.h"

namespace {

CompositeSpec make_composite_def() {
    CompositeSpec def;
    def.classname = "TestComposite";

    Port in;
in.direction = bp2::Direction::Input;
    in.type = PortType::Bool;
    in.domain = Domain::Logical;
    def.ports["in"] = in;

    Port out;
out.direction = bp2::Direction::Output;
    out.type = PortType::Bool;
    out.domain = Domain::Logical;
    def.ports["out"] = out;

    DeviceInstance src;
    src.name = "src";
    src.classname = "SourceNode";
    src.params["gain"] = "0.5";
    src.pos = std::make_pair(10.0f, 20.0f);
    src.size = std::make_pair(30.0f, 40.0f);

    DeviceInstance dst;
    dst.name = "dst";
    dst.classname = "SinkNode";

    def.devices.push_back(src);
    def.devices.push_back(dst);

    Connection conn;
    conn.from = "src.out";
    conn.to = "dst.in";
    conn.routing_points.push_back({1.0f, 2.0f});
    conn.routing_points.push_back({3.0f, 4.0f});
    def.connections.push_back(conn);

    return def;
}

ComponentRegistry make_registry() {
    ComponentRegistry registry;

    PrimitiveSpec src;
    src.classname = "SourceNode";
    Port src_out;
    src_out.direction = bp2::Direction::Output;
    src_out.type = PortType::Bool;
    src_out.domain = Domain::Logical;
    src.ports["out"] = src_out;
    registry.register_type(src.classname, src);

    PrimitiveSpec dst;
    dst.classname = "SinkNode";
    Port dst_in;
    dst_in.direction = bp2::Direction::Input;
    dst_in.type = PortType::Bool;
    dst_in.domain = Domain::Logical;
    dst.ports["in"] = dst_in;
    registry.register_type(dst.classname, dst);

    return registry;
}

} // namespace

TEST(TypeDefToBlueprint, PreservesLayoutAndRoutingAndDomain) {
    ui::StringInterner interner;
    CompositeSpec def = make_composite_def();
    ComponentRegistry registry = make_registry();

    bp2::Blueprint bp = bp2::blueprint_from_type_definition(def, interner, registry);

    const auto* src = bp.find_node(interner.lookup("src"));
    ASSERT_NE(src, nullptr);
    EXPECT_FLOAT_EQ(src->layout.x, 10.0f);
    EXPECT_FLOAT_EQ(src->layout.y, 20.0f);
    ASSERT_TRUE(src->layout.width.has_value());
    ASSERT_TRUE(src->layout.height.has_value());
    EXPECT_FLOAT_EQ(*src->layout.width, 30.0f);
    EXPECT_FLOAT_EQ(*src->layout.height, 40.0f);
    EXPECT_TRUE(src->semantic.params.contains(interner.intern("gain")));
    EXPECT_FLOAT_EQ(src->semantic.params.at(interner.intern("gain")), 0.5f);

    ASSERT_EQ(bp.wires().size(), 1u);
    const auto& wire = bp.wires().front();
    EXPECT_EQ(wire.domain, Domain::Logical);
    ASSERT_EQ(wire.routing_points.size(), 2u);
    EXPECT_FLOAT_EQ(wire.routing_points[0].first, 1.0f);
    EXPECT_FLOAT_EQ(wire.routing_points[0].second, 2.0f);
    EXPECT_FLOAT_EQ(wire.routing_points[1].first, 3.0f);
    EXPECT_FLOAT_EQ(wire.routing_points[1].second, 4.0f);
}

TEST(TypeDefToBlueprint, WireDomainMatchesResolvedEndpointInterface) {
    ui::StringInterner interner;
    CompositeSpec def = make_composite_def();
    ComponentRegistry registry = make_registry();
    auto* src_def = as_primitive_mut(*registry.get_mut("SourceNode"));
    ASSERT_NE(src_def, nullptr);
    src_def->ports["out"].type = PortType::V;
    src_def->ports["out"].domain = Domain::Electrical;

    auto* dst_def = as_primitive_mut(*registry.get_mut("SinkNode"));
    ASSERT_NE(dst_def, nullptr);
    dst_def->ports["in"].type = PortType::V;
    dst_def->ports["in"].domain = Domain::Electrical;

    bp2::Blueprint bp = bp2::blueprint_from_type_definition(def, interner, registry);
    ASSERT_EQ(bp.wires().size(), 1u);
    EXPECT_EQ(bp.wires().front().domain, Domain::Electrical);
}

TEST(TypeDefToBlueprint, MalformedBridgeMetadataFailsImport) {
    ui::StringInterner interner;
    ComponentRegistry registry = make_registry();

    PrimitiveSpec sink;
    sink.classname = "BoolSink";
    Port sink_in;
    sink_in.direction = bp2::Direction::Input;
    sink_in.type = PortType::Bool;
    sink_in.domain = Domain::Logical;
    sink.ports["in"] = sink_in;
    registry.register_type("BoolSink", sink);

    CompositeSpec def;
    def.classname = "BadComposite";

    Port exposed;
    exposed.direction = bp2::Direction::Input;
    exposed.type = PortType::Bool;
    exposed.domain = Domain::Logical;
    def.ports["other_flag"] = exposed;

    BridgePortDefinition bridge;
    bridge.id = "flag";
    bridge.exposed_port = "flag";
    bridge.direction = bp2::BridgeDirection::Input;
    bridge.type = PortType::Contextual;
    def.bridge_ports.push_back(bridge);

    DeviceInstance sink_dev;
    sink_dev.name = "sink";
    sink_dev.classname = "BoolSink";
    def.devices.push_back(sink_dev);

    Connection conn;
    conn.from = "flag.port";
    conn.to = "sink.in";
    def.connections.push_back(conn);

    EXPECT_THROW(bp2::blueprint_from_type_definition(def, interner, registry), std::runtime_error);
}

// Regression: as_composite_mut was used on PrimitiveSpec entries, silently
// skipping port mutations and producing wrong wire domain.
TEST(TypeDefToBlueprint, Regression_PrimitivePortMutationUsesCorrectAccessor) {
    ui::StringInterner interner;
    CompositeSpec def = make_composite_def();
    ComponentRegistry registry = make_registry();

    // Verify the registry entries are PrimitiveSpec, not CompositeSpec
    ASSERT_NE(as_primitive(registry.all_types().at("SourceNode")), nullptr);
    ASSERT_NE(as_primitive(registry.all_types().at("SinkNode")), nullptr);
    ASSERT_EQ(as_composite(registry.all_types().at("SourceNode")), nullptr);
    ASSERT_EQ(as_composite(registry.all_types().at("SinkNode")), nullptr);

    // Mutate via correct accessor
    auto* src = as_primitive_mut(*registry.get_mut("SourceNode"));
    ASSERT_NE(src, nullptr);
    src->ports["out"].type = PortType::V;
    src->ports["out"].domain = Domain::Electrical;

    auto* dst = as_primitive_mut(*registry.get_mut("SinkNode"));
    ASSERT_NE(dst, nullptr);
    dst->ports["in"].type = PortType::V;
    dst->ports["in"].domain = Domain::Electrical;

    bp2::Blueprint bp = bp2::blueprint_from_type_definition(def, interner, registry);
    ASSERT_EQ(bp.wires().size(), 1u);
    EXPECT_EQ(bp.wires().front().domain, Domain::Electrical);
}

TEST(TypeDefToBlueprint, RejectsUnknownDeviceClassInsteadOfSynthesizingIface) {
    ui::StringInterner interner;
    ComponentRegistry registry = make_registry();

    CompositeSpec def;
    def.classname = "BadComposite";

    DeviceInstance missing;
    missing.name = "ghost";
    missing.classname = "MissingDevice";
    missing.ports["out"] = Port{bp2::Direction::Output, PortType::Bool, Domain::Logical, false};
    def.devices.push_back(std::move(missing));

    EXPECT_THROW(bp2::blueprint_from_type_definition(def, interner, registry), std::runtime_error);
}

// --- Sub-blueprint validation tests ---

namespace {

/// Helper: creates a registry with SourceNode + SinkNode + a simple composite "Inner".
ComponentRegistry make_registry_with_inner_composite() {
    ComponentRegistry registry;

    // SourceNode (primitive)
    PrimitiveSpec src;
    src.classname = "SourceNode";
    Port src_out;
    src_out.direction = bp2::Direction::Output;
    src_out.type = PortType::Bool;
    src_out.domain = Domain::Logical;
    src.ports["out"] = src_out;
    registry.register_type(src.classname, src);

    // SinkNode (primitive)
    PrimitiveSpec dst;
    dst.classname = "SinkNode";
    Port dst_in;
    dst_in.direction = bp2::Direction::Input;
    dst_in.type = PortType::Bool;
    dst_in.domain = Domain::Logical;
    dst.ports["in"] = dst_in;
    registry.register_type(dst.classname, dst);

    // Inner composite: SourceNode → SinkNode
    CompositeSpec inner;
    inner.classname = "Inner";

    Port inner_in;
    inner_in.direction = bp2::Direction::Input;
    inner_in.type = PortType::Bool;
    inner_in.domain = Domain::Logical;
    inner.ports["in"] = inner_in;

    Port inner_out;
    inner_out.direction = bp2::Direction::Output;
    inner_out.type = PortType::Bool;
    inner_out.domain = Domain::Logical;
    inner.ports["out"] = inner_out;

    DeviceInstance inner_src;
    inner_src.name = "s";
    inner_src.classname = "SourceNode";
    inner.devices.push_back(inner_src);

    DeviceInstance inner_dst;
    inner_dst.name = "d";
    inner_dst.classname = "SinkNode";
    inner.devices.push_back(inner_dst);

    Connection inner_conn;
    inner_conn.from = "s.out";
    inner_conn.to = "d.in";
    inner.connections.push_back(inner_conn);

    registry.register_type(inner.classname, inner);

    return registry;
}

} // namespace

TEST(TypeDefToBlueprint, SubBlueprintRefUnknownTypeThrows) {
    ui::StringInterner interner;
    ComponentRegistry registry = make_registry_with_inner_composite();

    CompositeSpec outer;
    outer.classname = "Outer";

    SubBlueprintRef ref;
    ref.id = "missing_child";
    ref.type_name = "NonExistentComposite";  // Not in registry
    outer.sub_blueprints.push_back(ref);

    EXPECT_THROW(
        bp2::blueprint_from_type_definition(outer, interner, registry),
        std::runtime_error);
}

TEST(TypeDefToBlueprint, SubBlueprintRefPrimitiveTypeThrows) {
    ui::StringInterner interner;
    ComponentRegistry registry = make_registry_with_inner_composite();

    CompositeSpec outer;
    outer.classname = "Outer";

    SubBlueprintRef ref;
    ref.id = "not_a_composite";
    ref.type_name = "SourceNode";  // Primitive, not composite
    outer.sub_blueprints.push_back(ref);

    EXPECT_THROW(
        bp2::blueprint_from_type_definition(outer, interner, registry),
        std::runtime_error);
}

TEST(TypeDefToBlueprint, SubBlueprintRefValidCompositeCreatesNode) {
    ui::StringInterner interner;
    ComponentRegistry registry = make_registry_with_inner_composite();

    CompositeSpec outer;
    outer.classname = "Outer";

    SubBlueprintRef ref;
    ref.id = "child";
    ref.type_name = "Inner";
    ref.pos = std::make_pair(50.0f, 60.0f);
    ref.params_override["s.gain"] = "2.0";
    outer.sub_blueprints.push_back(ref);

    bp2::Blueprint bp = bp2::blueprint_from_type_definition(outer, interner, registry);

    const auto* child = bp.find_node(interner.lookup("child"));
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(child->is_blueprint_instance());
    EXPECT_EQ(child->semantic.type, interner.lookup("Inner"));
    EXPECT_FLOAT_EQ(child->layout.x, 50.0f);
    EXPECT_FLOAT_EQ(child->layout.y, 60.0f);

    // Interface is resolved lazily — verify resolve_node_iface works
    bp2::Interface iface = bp.resolve_node_iface(
        *child, bp2::Blueprint::NodeIfaceAuthority{interner, &registry});
    EXPECT_TRUE(iface.has(interner.lookup("in")));
    EXPECT_TRUE(iface.has(interner.lookup("out")));
}
