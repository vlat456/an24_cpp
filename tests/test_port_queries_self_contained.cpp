/// Regression test: port_traits.h and port_queries.h must be self-contained.
///
/// This TU includes port_queries.h (which itself includes port_traits.h).
/// If the generated headers are missing a required include (e.g. port_metadata.h),
/// this file will fail to compile — catching the bug at build time.
///
/// Historical note: port_traits.h uses PORT_META[] and COMPONENT_PORT_INFO[]
/// from port_metadata.h in get_output_ports() and get_source_writer_ports().
/// The include was initially missing — this test prevents regression.

#include <gtest/gtest.h>
#include "core/solvers/common/port_queries.h"

TEST(PortQueriesSelfContained, GetOutputPortsCompilesAndRuns) {
    // If port_traits.h doesn't include port_metadata.h, this won't compile
    // because get_output_ports uses PORT_META and COMPONENT_PORT_INFO.
    auto outputs = get_output_ports(ComponentKind::Value);
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0], "o");
}

TEST(PortQueriesSelfContained, GetSourceWriterPortsCompilesAndRuns) {
    // This function also depends on PORT_META, COMPONENT_PORT_INFO from
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

TEST(PortQueriesSelfContained, TraitPredicatesWork) {
    // Verify trait predicates are accessible through port_queries.h → port_traits.h chain.
    EXPECT_FALSE(is_scheduler_source_component(ComponentKind::AND));
    EXPECT_TRUE(is_solver_owned_electrical_component(ComponentKind::AZS));
    EXPECT_TRUE(requires_solver_role_component(ComponentKind::ControlledVoltageSource));
}
