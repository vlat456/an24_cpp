/// Regression test: port_queries.h must be self-contained.
///
/// This TU includes ONLY port_queries.h (not port_registry.h).
/// If the generated header is missing a required include (e.g. port_metadata.h),
/// this file will fail to compile — catching the bug at build time.
///
/// Historical note: port_queries.h uses *_PORT_COUNT, *_PORTS[], etc.
/// from port_metadata.h in get_output_ports() and get_source_writer_ports().
/// The include was initially missing — this test prevents regression.

#include <gtest/gtest.h>
#include "core/solvers/common/port_queries.h"

TEST(PortQueriesSelfContained, GetOutputPortsCompilesAndRuns) {
    // If port_queries.h doesn't include port_metadata.h, this won't compile
    // because get_output_ports uses *_PORT_COUNT, *_PORT_DIRECTIONS, *_PORTS.
    auto outputs = get_output_ports(ComponentKind::Value);
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0], "o");
}

TEST(PortQueriesSelfContained, GetSourceWriterPortsCompilesAndRuns) {
    // This function also depends on *_PORT_SOURCE_WRITER, *_PORT_DOMAINS from
    // port_metadata.h.
    auto sources = get_source_writer_ports(ComponentKind::ElectricalSource, 0xFF);
    ASSERT_FALSE(sources.empty());
    EXPECT_EQ(sources[0], "v_out");
}

TEST(PortQueriesSelfContained, GetComponentPortsCompilesAndRuns) {
    auto ports = get_component_ports(ComponentKind::AND);
    ASSERT_EQ(ports.size(), 3u);
    EXPECT_EQ(ports[0], "A");
    EXPECT_EQ(ports[1], "B");
    EXPECT_EQ(ports[2], "o");
}
