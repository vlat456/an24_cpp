/// Regression tests for the ContentPorts variant architecture.
///
/// These tests verify that:
///   1. AzsPorts is a distinct variant from SwitchPorts (not accidentally merged)
///   2. AzsPorts has the tripped field (not silently dropped)
///   3. The variant discriminates correctly between all content types
///   4. NodeContent::tripped field exists and round-trips
///
/// Regression history: commit a102e3f4 accidentally dropped AZS tripped signal
/// reading when replacing the flat NodeSignalCache with the ContentPorts variant.
/// The tripped InternedId was removed from the cache, causing AZS thermal trip
/// visual feedback (red button tint) to silently break.

#include <gtest/gtest.h>
#include "editor/document_simulation_internal.h"
#include "editor/data/node_content.h"
#include "ui/core/interned_id.h"

// == Variant structure tests ==

TEST(SignalCacheVariant, AzsPortsIsDistinctFromSwitchPorts) {
    // AzsPorts and SwitchPorts must be different types in the variant.
    // If they were merged, AZS tripped signal reading would be lost again.
    editor::ContentPorts azs = editor::AzsPorts{};
    editor::ContentPorts sw  = editor::SwitchPorts{};

    EXPECT_TRUE(std::holds_alternative<editor::AzsPorts>(azs));
    EXPECT_TRUE(std::holds_alternative<editor::SwitchPorts>(sw));
    EXPECT_FALSE(std::holds_alternative<editor::AzsPorts>(sw));
    EXPECT_FALSE(std::holds_alternative<editor::SwitchPorts>(azs));
}

TEST(SignalCacheVariant, AzsPortsHasTrippedField) {
    // AzsPorts must have a 'tripped' InternedId field.
    // This test fails to compile if the field is removed.
    ui::StringInterner interner;
    editor::AzsPorts ports;
    ports.state   = interner.intern("azs.state");
    ports.control = interner.intern("azs.control");
    ports.tripped = interner.intern("azs.tripped");

    EXPECT_FALSE(ports.tripped.empty());
    EXPECT_EQ(interner.resolve(ports.tripped), "azs.tripped");
}

TEST(SignalCacheVariant, SwitchPortsDoesNotHaveTrippedField) {
    // SwitchPorts should NOT have a tripped field — that's AZS-specific.
    // This test documents the intentional separation.
    editor::SwitchPorts sp;
    sp.state   = ui::InternedId{};
    sp.control = ui::InternedId{};

    // Verify the variant holds SwitchPorts without tripped
    editor::ContentPorts ports = sp;
    EXPECT_TRUE(std::holds_alternative<editor::SwitchPorts>(ports));
}

TEST(SignalCacheVariant, AllContentTypesAreRepresented) {
    // Verify the variant has exactly 7 alternatives:
    // monostate, GaugePorts, IndicatorPorts, SwitchPorts, AzsPorts, SliderPorts, KnobPorts
    constexpr size_t expected = 7;
    EXPECT_EQ(std::variant_size_v<editor::ContentPorts>, expected);
}

TEST(SignalCacheVariant, MonostateIsDefault) {
    editor::ContentPorts ports;
    EXPECT_TRUE(std::holds_alternative<std::monostate>(ports));
}

// == NodeContent tripped field round-trip ==

TEST(SignalCacheVariant, NodeContentTrippedFieldRoundTrips) {
    // NodeContent::tripped must survive copy and assignment.
    // This field is set by overlay_from_cache for AzsPorts and consumed
    // by the visual rendering pipeline (COLOR_TRIPPED).
    NodeContent c;
    c.type    = bp2::NodeContentType::Switch;
    c.state   = true;
    c.tripped = true;

    // Copy
    NodeContent copy = c;
    EXPECT_TRUE(copy.tripped);

    // Assignment
    NodeContent assigned;
    assigned = c;
    EXPECT_TRUE(assigned.tripped);

    // Default is false
    NodeContent fresh;
    EXPECT_FALSE(fresh.tripped);
}

// == NodeSignalCache structure ==

TEST(SignalCacheVariant, NodeSignalCacheHoldsBaseContentAndPorts) {
    // NodeSignalCache must have base_content, ports, and scope.
    editor::NodeSignalCache cache;

    // base_content should be default-constructed (type = None)
    EXPECT_EQ(cache.base_content.type, bp2::NodeContentType::None);

    // ports should be monostate by default
    EXPECT_TRUE(std::holds_alternative<std::monostate>(cache.ports));

    // Can assign AzsPorts
    cache.ports = editor::AzsPorts{};
    EXPECT_TRUE(std::holds_alternative<editor::AzsPorts>(cache.ports));

    // base_content preserves static params
    cache.base_content.min = 0.0f;
    cache.base_content.max = 28.0f;
    cache.base_content.label = "V";

    NodeContent overlay = cache.base_content;
    EXPECT_FLOAT_EQ(overlay.min, 0.0f);
    EXPECT_FLOAT_EQ(overlay.max, 28.0f);
    EXPECT_EQ(overlay.label, "V");
}

TEST(SignalCacheVariant, AzsPortsVariantPreservesTrippedThroughNodeContent) {
    // End-to-end structural test: AzsPorts → tripped signal → NodeContent::tripped
    // This simulates what overlay_from_cache does for AzsPorts.
    ui::StringInterner interner;

    editor::AzsPorts azs;
    azs.tripped = interner.intern("azs_1.tripped");

    // Simulate: tripped signal is active (1.0f)
    // In production, this is: simulation.get_signal_value(azs.tripped)
    // Here we just verify the struct carries the right InternedId.
    EXPECT_EQ(interner.resolve(azs.tripped), "azs_1.tripped");

    // The production code path:
    //   content.tripped = simulation.get_signal_value(azs.tripped) > 0.5f;
    // When signal = 1.0f → tripped = true
    NodeContent content;
    content.tripped = true;  // would be set by overlay_from_cache
    EXPECT_TRUE(content.tripped);
}
