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
    normal_node.id = interner.intern("battery_1");
    normal_node.type = interner.intern("Battery");
    normal_node.expandable = false;
    normal_node.blueprint_path = "";
    
    ui::InternedId battery_iid = normal_node.id;
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
    composite_node.id = interner.intern("firstorderlag_1");
    composite_node.type = interner.intern("FirstOrderLag");
    composite_node.expandable = true;
    composite_node.blueprint_path = "math/FirstOrderLag.blueprint";
    
    ui::InternedId composite_iid = composite_node.id;
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
    child_node.id = interner.intern("accumulator");
    child_node.type = interner.intern("Accumulator");
    child_node.expandable = false;
    
    ui::InternedId child_iid = child_node.id;
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
    composite_node.id = interner.intern("firstorderlag_1");
    composite_node.type = interner.intern("FirstOrderLag");
    composite_node.expandable = true;
    composite_node.blueprint_path = "math/FirstOrderLag.blueprint";
    
    ui::InternedId composite_iid = composite_node.id;
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
    node.id = interner.intern("battery_1");
    node.type = interner.intern("Battery");
    
    // Endpoint with empty node IID
    editor::SignalEndpoint endpoint_empty_node{&node, ui::InternedId(), interner.intern("V")};
    editor::SignalKeyContext context = editor::root_signal_context();
    
    std::string result_empty_node = editor::resolve_runtime_signal_key(bp, interner, endpoint_empty_node, context);
    EXPECT_EQ(result_empty_node, "")
        << "Resolver MUST return empty string when node_iid is empty";
    
    // Endpoint with empty port IID
    editor::SignalEndpoint endpoint_empty_port{&node, node.id, ui::InternedId()};
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

