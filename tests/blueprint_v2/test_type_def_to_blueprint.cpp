#include <gtest/gtest.h>

#include "blueprint_v2/library/type_def_to_blueprint.h"

namespace {

TypeDefinition make_composite_def() {
    TypeDefinition def;
    def.classname = "TestComposite";
    def.cpp_class = false;

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

TypeRegistry make_registry() {
    TypeRegistry registry;

    TypeDefinition src;
    src.classname = "SourceNode";
    src.cpp_class = true;
    Port src_out;
    src_out.direction = bp2::Direction::Output;
    src_out.type = PortType::Bool;
    src_out.domain = Domain::Logical;
    src.ports["out"] = src_out;
    registry.types[src.classname] = src;

    TypeDefinition dst;
    dst.classname = "SinkNode";
    dst.cpp_class = true;
    Port dst_in;
    dst_in.direction = bp2::Direction::Input;
    dst_in.type = PortType::Bool;
    dst_in.domain = Domain::Logical;
    dst.ports["in"] = dst_in;
    registry.types[dst.classname] = dst;

    return registry;
}

} // namespace

TEST(TypeDefToBlueprint, PreservesLayoutAndRoutingAndDomain) {
    ui::StringInterner interner;
    TypeDefinition def = make_composite_def();
    TypeRegistry registry = make_registry();

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
    TypeDefinition def = make_composite_def();
    TypeRegistry registry = make_registry();
    registry.types["SinkNode"].ports["in"].type = PortType::V;
    registry.types["SinkNode"].ports["in"].domain = Domain::Electrical;
    registry.types["SourceNode"].ports["out"].type = PortType::V;
    registry.types["SourceNode"].ports["out"].domain = Domain::Electrical;

    bp2::Blueprint bp = bp2::blueprint_from_type_definition(def, interner, registry);
    ASSERT_EQ(bp.wires().size(), 1u);
    EXPECT_EQ(bp.wires().front().domain, Domain::Electrical);
}

TEST(TypeDefToBlueprint, MalformedBridgeMetadataFailsImport) {
    ui::StringInterner interner;
    TypeRegistry registry = make_registry();

    TypeDefinition sink;
    sink.classname = "BoolSink";
    sink.cpp_class = true;
    Port sink_in;
    sink_in.direction = bp2::Direction::Input;
    sink_in.type = PortType::Bool;
    sink_in.domain = Domain::Logical;
    sink.ports["in"] = sink_in;
    registry.types["BoolSink"] = sink;

    TypeDefinition def;
    def.classname = "BadComposite";
    def.cpp_class = false;

    Port exposed;
    exposed.direction = bp2::Direction::Input;
    exposed.type = PortType::Bool;
    exposed.domain = Domain::Logical;
    def.ports["other_flag"] = exposed;

    BridgePortDefinition bridge;
    bridge.id = "flag";
    bridge.exposed_port = "flag";
    bridge.side = bp2::Direction::Input;
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
