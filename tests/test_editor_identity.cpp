/// @file test_editor_identity.cpp
/// Regression tests for editor::NodeId type-safety wrapper (Issue #45).
///
/// Compile-time safety note:
/// The following must NOT compile (verified manually, not in test binary):
///
///   editor::NodeId nid = "battery_1";           // ERROR: implicit conversion
///   editor::NodeId nid("battery_1");             // ERROR: constructor is private
///   std::string s = editor::NodeId::from_string("x"); // ERROR: no implicit conversion to string
///
/// These tests verify runtime semantics: construction, round-trip, equality, emptiness.

#include <gtest/gtest.h>
#include "editor/identity.h"
#include <string>

// ── Construction & round-trip ──

TEST(EditorNodeId, FromString_RoundTrip) {
    auto nid = editor::NodeId::from_string("battery_1");
    EXPECT_EQ(nid.str(), "battery_1");
    EXPECT_FALSE(nid.empty());
}

TEST(EditorNodeId, DefaultConstructed_IsEmpty) {
    editor::NodeId nid;
    EXPECT_TRUE(nid.empty());
    EXPECT_EQ(nid.str(), "");
}

TEST(EditorNodeId, FromEmptyString_IsEmpty) {
    auto nid = editor::NodeId::from_string("");
    EXPECT_TRUE(nid.empty());
}

// ── Equality ──

TEST(EditorNodeId, Equality_SameValue) {
    auto a = editor::NodeId::from_string("switch_1");
    auto b = editor::NodeId::from_string("switch_1");
    EXPECT_EQ(a, b);
}

TEST(EditorNodeId, Equality_DifferentValue) {
    auto a = editor::NodeId::from_string("switch_1");
    auto b = editor::NodeId::from_string("switch_2");
    EXPECT_NE(a, b);
}

TEST(EditorNodeId, Equality_EmptyVsNonEmpty) {
    editor::NodeId empty;
    auto non_empty = editor::NodeId::from_string("x");
    EXPECT_NE(empty, non_empty);
}

// ── Copy & move ──

TEST(EditorNodeId, CopySemantics) {
    auto a = editor::NodeId::from_string("resistor_1");
    editor::NodeId b = a;  // copy
    EXPECT_EQ(a, b);
    EXPECT_EQ(b.str(), "resistor_1");
}

TEST(EditorNodeId, MoveSemantics) {
    auto a = editor::NodeId::from_string("capacitor_1");
    editor::NodeId b = std::move(a);
    EXPECT_EQ(b.str(), "capacitor_1");
    // a is moved-from — just verify it doesn't crash
    EXPECT_TRUE(a.empty() || !a.empty());
}

// ── No implicit conversion (static_assert style comments) ──
// The following lines are commented out. Uncomment any one to verify
// it produces a compile error — this is the type-safety guarantee.
//
// TEST(EditorNodeId, CompileError_ImplicitFromString) {
//     editor::NodeId nid = std::string("oops");  // Should NOT compile
// }
//
// TEST(EditorNodeId, CompileError_ImplicitToString) {
//     auto nid = editor::NodeId::from_string("x");
//     std::string s = nid;  // Should NOT compile
// }
