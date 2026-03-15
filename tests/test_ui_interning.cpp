#include <gtest/gtest.h>
#include "ui/core/interned_id.h"

#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

using ui::InternedId;
using ui::StringInterner;

// =============================================================================
// InternedId — Value Type Tests
// =============================================================================

TEST(InternedId, DefaultConstructedIsEmpty) {
    InternedId id;
    EXPECT_TRUE(id.empty());
}

TEST(InternedId, EmptyIdsAreEqual) {
    InternedId a, b;
    EXPECT_EQ(a, b);
}

TEST(InternedId, NonEmptyIsNotEmpty) {
    StringInterner interner;
    auto id = interner.intern("hello");
    EXPECT_FALSE(id.empty());
}

TEST(InternedId, EqualityForSameString) {
    StringInterner interner;
    auto a = interner.intern("voltage");
    auto b = interner.intern("voltage");
    EXPECT_EQ(a, b);
}

TEST(InternedId, InequalityForDifferentStrings) {
    StringInterner interner;
    auto a = interner.intern("voltage");
    auto b = interner.intern("current");
    EXPECT_NE(a, b);
}

TEST(InternedId, LessThanOrdering) {
    StringInterner interner;
    auto a = interner.intern("aaa");
    auto b = interner.intern("bbb");
    // Just verify that one is less than the other (deterministic total order)
    EXPECT_TRUE(a < b || b < a);
    // And that neither is less than itself
    EXPECT_FALSE(a < a);
}

TEST(InternedId, CopySemantics) {
    StringInterner interner;
    auto a = interner.intern("test");
    InternedId b = a;  // copy construct
    EXPECT_EQ(a, b);

    InternedId c;
    c = a;  // copy assign
    EXPECT_EQ(a, c);
}

TEST(InternedId, TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<InternedId>,
                  "InternedId must be trivially copyable for cache performance");
}

TEST(InternedId, SizeIsUint32) {
    static_assert(sizeof(InternedId) == sizeof(uint32_t),
                  "InternedId should be exactly 4 bytes");
}

// =============================================================================
// InternedId — Hashing (usable in unordered containers)
// =============================================================================

TEST(InternedId, UsableInUnorderedSet) {
    StringInterner interner;
    auto a = interner.intern("node_1");
    auto b = interner.intern("node_2");
    auto c = interner.intern("node_1");  // same as a

    std::unordered_set<InternedId> set;
    set.insert(a);
    set.insert(b);
    set.insert(c);  // duplicate of a
    EXPECT_EQ(set.size(), 2u);
}

TEST(InternedId, UsableAsUnorderedMapKey) {
    StringInterner interner;
    auto a = interner.intern("key_a");
    auto b = interner.intern("key_b");

    std::unordered_map<InternedId, int> map;
    map[a] = 10;
    map[b] = 20;
    EXPECT_EQ(map[a], 10);
    EXPECT_EQ(map[b], 20);
}

// =============================================================================
// StringInterner — Core Behavior
// =============================================================================

TEST(StringInterner, InternReturnsNonEmpty) {
    StringInterner interner;
    auto id = interner.intern("hello");
    EXPECT_FALSE(id.empty());
}

TEST(StringInterner, DuplicateInternReturnsSameId) {
    StringInterner interner;
    auto a = interner.intern("bus_main");
    auto b = interner.intern("bus_main");
    EXPECT_EQ(a, b);
}

TEST(StringInterner, DifferentStringsGetDifferentIds) {
    StringInterner interner;
    auto a = interner.intern("alpha");
    auto b = interner.intern("beta");
    EXPECT_NE(a, b);
}

TEST(StringInterner, ResolveRoundTrip) {
    StringInterner interner;
    auto id = interner.intern("round_trip_test");
    std::string_view resolved = interner.resolve(id);
    EXPECT_EQ(resolved, "round_trip_test");
}

TEST(StringInterner, ResolveEmptyIdReturnsEmptyView) {
    StringInterner interner;
    InternedId empty;
    std::string_view resolved = interner.resolve(empty);
    EXPECT_TRUE(resolved.empty());
}

TEST(StringInterner, ResolveMultipleStrings) {
    StringInterner interner;
    auto id_a = interner.intern("node_battery");
    auto id_b = interner.intern("node_pump");
    auto id_c = interner.intern("wire_001");

    EXPECT_EQ(interner.resolve(id_a), "node_battery");
    EXPECT_EQ(interner.resolve(id_b), "node_pump");
    EXPECT_EQ(interner.resolve(id_c), "wire_001");
}

TEST(StringInterner, InternEmptyStringReturnsEmptyId) {
    StringInterner interner;
    auto id = interner.intern("");
    EXPECT_TRUE(id.empty());
}

TEST(StringInterner, InternFromStringView) {
    StringInterner interner;
    std::string_view sv = "from_view";
    auto id = interner.intern(sv);
    EXPECT_EQ(interner.resolve(id), "from_view");
}

TEST(StringInterner, InternFromStdString) {
    StringInterner interner;
    std::string s = "from_string";
    auto id = interner.intern(s);
    EXPECT_EQ(interner.resolve(id), "from_string");
}

TEST(StringInterner, InternFromCString) {
    StringInterner interner;
    const char* cs = "from_cstr";
    auto id = interner.intern(cs);
    EXPECT_EQ(interner.resolve(id), "from_cstr");
}

// =============================================================================
// StringInterner — Pointer Stability
// =============================================================================

TEST(StringInterner, ResolvedViewRemainsValidAfterMoreInterns) {
    StringInterner interner;
    auto id_first = interner.intern("first_string");
    std::string_view view_first = interner.resolve(id_first);

    // Intern many more strings to potentially trigger reallocation
    for (int i = 0; i < 10000; ++i) {
        interner.intern("bulk_" + std::to_string(i));
    }

    // The original view must still be valid
    EXPECT_EQ(view_first, "first_string");
    EXPECT_EQ(interner.resolve(id_first), "first_string");
}

// =============================================================================
// StringInterner — Size / Contains
// =============================================================================

TEST(StringInterner, SizeReflectsUniqueStrings) {
    StringInterner interner;
    EXPECT_EQ(interner.size(), 0u);

    interner.intern("a");
    EXPECT_EQ(interner.size(), 1u);

    interner.intern("b");
    EXPECT_EQ(interner.size(), 2u);

    interner.intern("a");  // duplicate
    EXPECT_EQ(interner.size(), 2u);
}

TEST(StringInterner, ContainsChecksByString) {
    StringInterner interner;
    EXPECT_FALSE(interner.contains("missing"));

    interner.intern("present");
    EXPECT_TRUE(interner.contains("present"));
    EXPECT_FALSE(interner.contains("still_missing"));
}

// =============================================================================
// StringInterner — Bulk interning (simulating real workload)
// =============================================================================

TEST(StringInterner, BulkIntern1000UniqueStrings) {
    StringInterner interner;
    std::vector<InternedId> ids;
    ids.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
        ids.push_back(interner.intern("node_" + std::to_string(i)));
    }

    EXPECT_EQ(interner.size(), 1000u);

    // All IDs should be unique
    std::unordered_set<InternedId> unique_ids(ids.begin(), ids.end());
    EXPECT_EQ(unique_ids.size(), 1000u);

    // Round-trip all of them
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(interner.resolve(ids[i]), "node_" + std::to_string(i));
    }
}

// =============================================================================
// InternedId — Sorting
// =============================================================================

TEST(InternedId, SortableInVector) {
    StringInterner interner;
    auto c = interner.intern("charlie");
    auto a = interner.intern("alpha");
    auto b = interner.intern("bravo");

    std::vector<InternedId> vec = {c, a, b};
    std::sort(vec.begin(), vec.end());

    // After sort, should be in deterministic order (by underlying uint32_t)
    EXPECT_TRUE(vec[0] < vec[1] || vec[0] == vec[1]);
    EXPECT_TRUE(vec[1] < vec[2] || vec[1] == vec[2]);
}

// =============================================================================
// Multiple Interner Instances (isolation)
// =============================================================================

TEST(StringInterner, TwoInternerInstancesAreIsolated) {
    StringInterner interner_a;
    StringInterner interner_b;

    auto id_a = interner_a.intern("shared_name");
    auto id_b = interner_b.intern("shared_name");

    // Both should resolve correctly in their own interner
    EXPECT_EQ(interner_a.resolve(id_a), "shared_name");
    EXPECT_EQ(interner_b.resolve(id_b), "shared_name");

    // The raw uint32_t values may happen to be equal (both first intern),
    // but they belong to different interners. This is fine — InternedId
    // is only meaningful within the context of its interner.
}
