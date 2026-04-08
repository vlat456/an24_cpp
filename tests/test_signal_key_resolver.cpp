#include <gtest/gtest.h>

#include "editor/signal_key_resolver.h"
#include "editor/external_ref_mapping.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

// =============================================================================
// Unit tests: signal key resolver
// These tests exercise the resolver and helper functions.
// The main integration tests use the external_ref_signal_mapping tests
// which run the full simulator with signal values.
// =============================================================================

// Test 1: Helper function build_signal_key
TEST(SignalKeyResolver, BuildSignalKey) {
    std::string result = editor::build_signal_key("battery_1", "V");
    EXPECT_EQ(result, "battery_1.V");
}

// Test 2: Helper function resolve_external_ref_signal_key
TEST(SignalKeyResolver, ResolveExternalRefSignalKey) {
    std::string result = editor::resolve_external_ref_signal_key("firstorderlag_1", "accumulator.out");
    EXPECT_EQ(result, "firstorderlag_1:accumulator.out");
}

// Test 3: Helper function map_composite_port_key
TEST(SignalKeyResolver, MapCompositePortKey) {
    std::string result = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_EQ(result, "firstorderlag_1:out.ext");
}

// Test 4: build_signal_key with various inputs
TEST(SignalKeyResolver, BuildSignalKeyVariants) {
    EXPECT_EQ(editor::build_signal_key("node_1", "V"), "node_1.V");
    EXPECT_EQ(editor::build_signal_key("relay_1", "I"), "relay_1.I");
    EXPECT_EQ(editor::build_signal_key("in", "port"), "in.port");
    EXPECT_EQ(editor::build_signal_key("accumulator", "out"), "accumulator.out");
}

// Test 5: resolve_external_ref_signal_key with nested keys
TEST(SignalKeyResolver, ResolveExternalRefNested) {
    // Multi-level nesting shouldn't happen but the function should handle it
    std::string result = editor::resolve_external_ref_signal_key("outer_1", "inner_2:multiply.A");
    EXPECT_EQ(result, "outer_1:inner_2:multiply.A");
}

// Test 6: map_composite_port_key with different ports
TEST(SignalKeyResolver, MapCompositePortKeyVariants) {
    EXPECT_EQ(editor::map_composite_port_key("firstorderlag_1", "in"), "firstorderlag_1:in.ext");
    EXPECT_EQ(editor::map_composite_port_key("firstorderlag_1", "out"), "firstorderlag_1:out.ext");
    EXPECT_EQ(editor::map_composite_port_key("firstorderlag_1", "rate"), "firstorderlag_1:rate.ext");
    EXPECT_EQ(editor::map_composite_port_key("my_filter_2", "output"), "my_filter_2:output.ext");
}

// Test 7: Round-trip: build + resolve_external_ref
TEST(SignalKeyResolver, RoundTripExternalRef) {
    std::string child_key = editor::build_signal_key("accumulator", "out");
    std::string parent_key = editor::resolve_external_ref_signal_key("firstorderlag_1", child_key);
    EXPECT_EQ(parent_key, "firstorderlag_1:accumulator.out");
}

// Test 8: Empty components
TEST(SignalKeyResolver, EmptyComponents) {
    EXPECT_EQ(editor::build_signal_key("", "port"), ".port");
    EXPECT_EQ(editor::build_signal_key("node", ""), "node.");
    EXPECT_EQ(editor::resolve_external_ref_signal_key("", "node.port"), ":node.port");
    EXPECT_EQ(editor::resolve_external_ref_signal_key("parent", ""), "parent:");
    EXPECT_EQ(editor::map_composite_port_key("", "port"), ":port.ext");
    EXPECT_EQ(editor::map_composite_port_key("node", ""), "node:.ext");
}

// Test 9: Signature of resolve_runtime_signal_key (compile check)
TEST(SignalKeyResolver, ResolveRuntimeSignatureCheck) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    // Just verify the function can be called with proper types
    // We can't easily test the interner behavior without proper setup
    auto node_id = ui::InternedId();
    auto port_id = ui::InternedId();
    editor::SignalEndpoint endpoint{nullptr, node_id, port_id};
    editor::SignalKeyContext context{editor::SignalKeyContextMode::Root, ""};
    
    // This should compile and not crash (even if result is empty)
    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    // Result will be empty or minimal since IDs are empty, but no crash
    EXPECT_TRUE(true);
}

// Test 10: Context modes compile and exist
TEST(SignalKeyResolver, ContextModes) {
    editor::SignalKeyContext root_ctx{editor::SignalKeyContextMode::Root, ""};
    EXPECT_EQ(root_ctx.mode, editor::SignalKeyContextMode::Root);
    
    editor::SignalKeyContext ext_ctx{editor::SignalKeyContextMode::ExternalReference, "parent_1"};
    EXPECT_EQ(ext_ctx.mode, editor::SignalKeyContextMode::ExternalReference);
    EXPECT_EQ(ext_ctx.parent_instance_id, "parent_1");
}

// ===========================================================================
// TABLE-DRIVEN REGRESSION TESTS: Signal resolution in visual + external paths
// ===========================================================================

namespace {

struct SignalKeyTestCase {
    std::string test_name;
    bool is_normal_node;  // true: normal, false: expandable
    std::string context_mode;  // "root" or "external"
    std::string parent_instance_id;
    std::string expected_key;
};

} // namespace

// Test 11: Table-driven resolver coverage for root + normal node
TEST(SignalKeyResolver, ResolveRuntimeKey_RootNormalNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    // Create a normal (non-expandable) node
    bp2::Blueprint::Node normal_node;
    normal_node.semantic.id = interner.intern("battery_1");
    normal_node.semantic.type = interner.intern("Battery");
    normal_node.view.expandable = false;
    normal_node.view.blueprint_path = "";
    
    ui::InternedId battery_iid = normal_node.semantic.id;
    ui::InternedId port_iid = interner.intern("V");
    
    editor::SignalEndpoint endpoint{&normal_node, battery_iid, port_iid};
    editor::SignalKeyContext context = editor::root_signal_context();
    
    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    EXPECT_EQ(result, "battery_1.V");
}

// Test 12: Table-driven resolver coverage for root + expandable composite
TEST(SignalKeyResolver, ResolveRuntimeKey_RootExpandableNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    // Create an expandable composite node
    bp2::Blueprint::Node composite_node;
    composite_node.semantic.id = interner.intern("firstorderlag_1");
    composite_node.semantic.type = interner.intern("FirstOrderLag");
    composite_node.view.expandable = true;
    composite_node.view.blueprint_path = "math/FirstOrderLag.blueprint";
    
    ui::InternedId composite_iid = composite_node.semantic.id;
    ui::InternedId port_iid = interner.intern("out");
    
    editor::SignalEndpoint endpoint{&composite_node, composite_iid, port_iid};
    editor::SignalKeyContext context = editor::root_signal_context();
    
    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    // Root-level expandable: "firstorderlag_1:out.ext"
    EXPECT_EQ(result, "firstorderlag_1:out.ext");
}

// Test 13: Table-driven resolver coverage for ExternalReference mode
TEST(SignalKeyResolver, ResolveRuntimeKey_ExternalReferenceMode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    // Child blueprint node: e.g., "accumulator" inside the composite
    bp2::Blueprint::Node child_node;
    child_node.semantic.id = interner.intern("accumulator");
    child_node.semantic.type = interner.intern("Accumulator");
    child_node.view.expandable = false;
    
    ui::InternedId child_iid = child_node.semantic.id;
    ui::InternedId port_iid = interner.intern("out");
    
    editor::SignalEndpoint endpoint{&child_node, child_iid, port_iid};
    editor::SignalKeyContext context = editor::external_ref_signal_context("firstorderlag_1");
    
    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    // External reference: "firstorderlag_1:accumulator.out"
    EXPECT_EQ(result, "firstorderlag_1:accumulator.out");
}

// Test 14: Regression test guarding expandable root path
TEST(SignalKeyResolver, ExpandableRootNeverUsesRawNodeDotPort) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    // Expandable node at root level
    bp2::Blueprint::Node composite_node;
    composite_node.semantic.id = interner.intern("firstorderlag_1");
    composite_node.semantic.type = interner.intern("FirstOrderLag");
    composite_node.view.expandable = true;
    composite_node.view.blueprint_path = "math/FirstOrderLag.blueprint";
    
    ui::InternedId composite_iid = composite_node.semantic.id;
    ui::InternedId port_iid = interner.intern("out");
    
    editor::SignalEndpoint endpoint{&composite_node, composite_iid, port_iid};
    editor::SignalKeyContext context = editor::root_signal_context();
    
    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    
    // MUST NOT be the raw "node.port" form
    EXPECT_NE(result, "firstorderlag_1.out")
        << "Expandable root output MUST use ':' expansion, not '.' form";
    
    // MUST be the expanded form
    EXPECT_EQ(result, "firstorderlag_1:out.ext")
        << "Expandable root output MUST resolve to expanded form";
}

// Test 15: Empty endpoint IDs are rejected by resolver (defensive check)
TEST(SignalKeyResolver, EmptyEndpointIds_ReturnsEmpty) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("battery_1");
    node.semantic.type = interner.intern("Battery");
    
    // Endpoint with empty node IID
    editor::SignalEndpoint endpoint_empty_node{&node, ui::InternedId(), interner.intern("V")};
    editor::SignalKeyContext context = editor::root_signal_context();
    
    std::string result_empty_node = editor::resolve_runtime_signal_key(bp, interner, endpoint_empty_node, context);
    EXPECT_EQ(result_empty_node, "")
        << "Resolver MUST return empty string when node_iid is empty";
    
    // Endpoint with empty port IID
    editor::SignalEndpoint endpoint_empty_port{&node, node.semantic.id, ui::InternedId()};
    std::string result_empty_port = editor::resolve_runtime_signal_key(bp, interner, endpoint_empty_port, context);
    EXPECT_EQ(result_empty_port, "")
        << "Resolver MUST return empty string when port_iid is empty";
}

// Test 16: Fallback raw key helper consistency across usage sites
TEST(SignalKeyResolver, FallbackRawKeyHelper_ConsistentFormat) {
    // Test that build_signal_key produces the same format
    // that would be used as a fallback in canvas_renderer
    std::string raw1 = editor::build_signal_key("battery_1", "V");
    std::string raw2 = editor::build_signal_key("firstorderlag_1", "in");
    
    EXPECT_EQ(raw1, "battery_1.V");
    EXPECT_EQ(raw2, "firstorderlag_1.in");
    
    // This same format is used as fallback when resolver returns empty
    // or when IDs are unavailable. Consistency is critical for continuity.
}

// ===========================================================================
// BRIDGE NODE NAMING CONVENTION REGRESSION TESTS (Issue #37)
// ===========================================================================

// Test 17: Embedded bridge node with colon convention is resolved
TEST(SignalKeyResolver, EmbeddedBridgeNode_ColonConvention_Found) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    // Simulate an embedded composite "gp_1" with a bridge node "gp_1:v_in"
    bp2::Blueprint::Node bridge_node;
    bridge_node.semantic.id = interner.intern("gp_1:v_in");
    bridge_node.semantic.type = interner.intern("BlueprintInput");
    bridge_node.view.name = "v_in";
    bridge_node.layout.group_id = "gp_1";
    bp = bp.with_node(std::move(bridge_node));

    // Create expandable proxy node
    bp2::Blueprint::Node proxy_node;
    proxy_node.semantic.id = interner.intern("gp_1");
    proxy_node.semantic.type = interner.intern("GroundPower");
    proxy_node.view.expandable = true;
    proxy_node.view.blueprint_path = "electrical/GroundPower.blueprint";
    proxy_node.view.outputs.emplace_back(interner.intern("v_in"), bp2::PortSide::Input, PortType::V);

    // Create nested entry
    bp2::Blueprint::Nested nested;
    nested.id = proxy_node.semantic.id;
    nested.blueprint_id = interner.intern("GroundPower");
    nested.embedded = true;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(proxy_node));

    ui::InternedId proxy_iid = interner.intern("gp_1");
    ui::InternedId port_iid = interner.intern("v_in");

    const auto* proxy = bp.find_node(proxy_iid);
    ASSERT_NE(proxy, nullptr);

    editor::SignalEndpoint endpoint{proxy, proxy_iid, port_iid};
    editor::SignalKeyContext context = editor::root_signal_context();

    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    // Must resolve via bridge node "gp_1:v_in" → "gp_1:v_in.ext"
    EXPECT_EQ(result, "gp_1:v_in.ext")
        << "Embedded proxy port must resolve to colon-convention bridge node";
}

// Test 18: Underscore-convention bridge nodes are NOT found by resolver
// This is a regression guard: the old addBlueprint created "gp_1_v_in" nodes.
// After the fix, the resolver should NOT find them.
TEST(SignalKeyResolver, EmbeddedBridgeNode_UnderscoreConvention_NotFound) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    // Create a bridge node with the OLD underscore convention
    bp2::Blueprint::Node bad_bridge;
    bad_bridge.semantic.id = interner.intern("gp_1_v_in");
    bad_bridge.semantic.type = interner.intern("BlueprintInput");
    bad_bridge.view.name = "v_in";
    bad_bridge.layout.group_id = "gp_1";
    bp = bp.with_node(std::move(bad_bridge));

    // Create expandable proxy node
    bp2::Blueprint::Node proxy_node;
    proxy_node.semantic.id = interner.intern("gp_1");
    proxy_node.semantic.type = interner.intern("GroundPower");
    proxy_node.view.expandable = true;
    proxy_node.view.blueprint_path = "electrical/GroundPower.blueprint";
    proxy_node.view.outputs.emplace_back(interner.intern("v_in"), bp2::PortSide::Input, PortType::V);

    bp2::Blueprint::Nested nested;
    nested.id = proxy_node.semantic.id;
    nested.blueprint_id = interner.intern("GroundPower");
    nested.embedded = true;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(proxy_node));

    ui::InternedId proxy_iid = interner.intern("gp_1");
    ui::InternedId port_iid = interner.intern("v_in");

    const auto* proxy = bp.find_node(proxy_iid);
    ASSERT_NE(proxy, nullptr);

    editor::SignalEndpoint endpoint{proxy, proxy_iid, port_iid};
    editor::SignalKeyContext context = editor::root_signal_context();

    std::string result = editor::resolve_runtime_signal_key(bp, interner, endpoint, context);
    // The underscore node should NOT be found by the resolver.
    // It should fall through to the map_composite_port_key fallback.
    EXPECT_NE(result, "gp_1_v_in.ext")
        << "Resolver must NOT find underscore-convention bridge nodes";
    EXPECT_EQ(result, "gp_1:v_in.ext")
        << "Resolver should fall back to composite port key format";
}

// Test 19: Bridge node naming - colon convention is canonical for composites
// When get_port_value() looks up a composite port, it uses the colon convention.
TEST(SignalKeyResolver, CompositePortKey_UsesColonConvention) {
    // Verify that map_composite_port_key always uses colon
    std::string key1 = editor::map_composite_port_key("GroundPower_1", "v_in");
    EXPECT_EQ(key1, "GroundPower_1:v_in.ext");
    EXPECT_NE(key1.find(':'), std::string::npos)
        << "Composite port key MUST contain colon separator";
    EXPECT_EQ(key1.find('_'), std::string("GroundPower").size())
        << "Only the instance suffix underscore should exist, not a port separator underscore";

    std::string key2 = editor::map_composite_port_key("12SAM28_1", "v_out");
    EXPECT_EQ(key2, "12SAM28_1:v_out.ext");
}

// ===========================================================================
// Test 20: Multiple bridge nodes resolve independently (addBlueprint scenario)
// Simulates what addBlueprint() produces: a composite with both input and
// output bridge nodes using colon convention, plus internal nodes using
// underscore convention. Verifies each port resolves to the correct bridge.
// ===========================================================================
TEST(SignalKeyResolver, MultipleBridgeNodes_ResolveIndependently) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    // Simulate addBlueprint() output for "gp_1" (GroundPower instance):
    //   gp_1:v_in   → BlueprintInput  (colon convention, bridge)
    //   gp_1:v_out  → BlueprintOutput (colon convention, bridge)
    //   gp_1_src    → ControlledVoltageSource (underscore convention, internal)

    bp2::Blueprint::Node bridge_in;
    bridge_in.semantic.id = interner.intern("gp_1:v_in");
    bridge_in.semantic.type = interner.intern("BlueprintInput");
    bridge_in.view.name = "v_in";
    bridge_in.layout.group_id = "gp_1";
    bp = bp.with_node(std::move(bridge_in));

    bp2::Blueprint::Node bridge_out;
    bridge_out.semantic.id = interner.intern("gp_1:v_out");
    bridge_out.semantic.type = interner.intern("BlueprintOutput");
    bridge_out.view.name = "v_out";
    bridge_out.layout.group_id = "gp_1";
    bp = bp.with_node(std::move(bridge_out));

    bp2::Blueprint::Node internal;
    internal.semantic.id = interner.intern("gp_1_src");
    internal.semantic.type = interner.intern("ControlledVoltageSource");
    internal.view.name = "src";
    internal.layout.group_id = "gp_1";
    bp = bp.with_node(std::move(internal));

    // Create expandable proxy
    bp2::Blueprint::Node proxy;
    proxy.semantic.id = interner.intern("gp_1");
    proxy.semantic.type = interner.intern("GroundPower");
    proxy.view.expandable = true;
    proxy.view.blueprint_path = "electrical/GroundPower.blueprint";
    proxy.view.outputs.emplace_back(interner.intern("v_in"), bp2::PortSide::Input, PortType::V);
    proxy.view.outputs.emplace_back(interner.intern("v_out"), bp2::PortSide::Output, PortType::V);

    bp2::Blueprint::Nested nested;
    nested.id = proxy.semantic.id;
    nested.blueprint_id = interner.intern("GroundPower");
    nested.embedded = true;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(proxy));

    // Resolve v_in port → must find gp_1:v_in bridge
    {
        const auto* p = bp.find_node(interner.intern("gp_1"));
        ASSERT_NE(p, nullptr);
        editor::SignalEndpoint ep{p, interner.intern("gp_1"), interner.intern("v_in")};
        std::string key = editor::resolve_runtime_signal_key(
            bp, interner, ep, editor::root_signal_context());
        EXPECT_EQ(key, "gp_1:v_in.ext");
    }

    // Resolve v_out port → must find gp_1:v_out bridge
    {
        const auto* p = bp.find_node(interner.intern("gp_1"));
        ASSERT_NE(p, nullptr);
        editor::SignalEndpoint ep{p, interner.intern("gp_1"), interner.intern("v_out")};
        std::string key = editor::resolve_runtime_signal_key(
            bp, interner, ep, editor::root_signal_context());
        EXPECT_EQ(key, "gp_1:v_out.ext");
    }

    // Internal node "gp_1_src" should NOT be found as a bridge for any port
    {
        const auto* p = bp.find_node(interner.intern("gp_1"));
        ASSERT_NE(p, nullptr);
        editor::SignalEndpoint ep{p, interner.intern("gp_1"), interner.intern("src")};
        std::string key = editor::resolve_runtime_signal_key(
            bp, interner, ep, editor::root_signal_context());
        // "src" is not a bridge port, so resolver falls through to composite key
        EXPECT_EQ(key, "gp_1:src.ext");
        // Critically, it must NOT resolve to "gp_1_src.ext" (underscore)
        EXPECT_NE(key, "gp_1_src.ext");
    }
}

// ===========================================================================
// Test 21: Bridge node with underscore-heavy proxy ID (edge case)
// Verifies that proxy IDs containing underscores (e.g. "ground_power_1")
// don't confuse the colon-based bridge lookup.
// ===========================================================================
TEST(SignalKeyResolver, BridgeNode_ProxyIdWithUnderscores_ColonStillWorks) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    // Proxy ID itself contains underscores
    bp2::Blueprint::Node bridge;
    bridge.semantic.id = interner.intern("ground_power_1:v_in");
    bridge.semantic.type = interner.intern("BlueprintInput");
    bridge.view.name = "v_in";
    bridge.layout.group_id = "ground_power_1";
    bp = bp.with_node(std::move(bridge));

    bp2::Blueprint::Node proxy;
    proxy.semantic.id = interner.intern("ground_power_1");
    proxy.semantic.type = interner.intern("GroundPower");
    proxy.view.expandable = true;
    proxy.view.blueprint_path = "electrical/GroundPower.blueprint";
    proxy.view.outputs.emplace_back(interner.intern("v_in"), bp2::PortSide::Input, PortType::V);

    bp2::Blueprint::Nested nested;
    nested.id = proxy.semantic.id;
    nested.blueprint_id = interner.intern("GroundPower");
    nested.embedded = true;
    bp = bp.with_nested(std::move(nested));
    bp = bp.with_node(std::move(proxy));

    const auto* p = bp.find_node(interner.intern("ground_power_1"));
    ASSERT_NE(p, nullptr);
    editor::SignalEndpoint ep{p, interner.intern("ground_power_1"), interner.intern("v_in")};
    std::string key = editor::resolve_runtime_signal_key(
        bp, interner, ep, editor::root_signal_context());
    EXPECT_EQ(key, "ground_power_1:v_in.ext")
        << "Colon convention must work even when proxy ID contains underscores";
}

