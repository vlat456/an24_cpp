#include <gtest/gtest.h>

#include "editor/signal_key_resolver.h"
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


