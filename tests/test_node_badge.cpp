// Unit tests for NodeBadgeSet bitmask, BadgeVisuals lookup,
// codepoint_to_utf8 encoding, and IconFont::available().
//
// Covers: #368 (NodeBadgeSet), #369 (BadgeVisuals + codepoint_to_utf8 + IconFont)

#include <gtest/gtest.h>
#include "editor/visual/presentation/node_badge.h"

using editor::NodeBadge;
using editor::NodeBadgeSet;
using editor::BadgeVisuals;
using editor::get_badge_visuals;
using editor::IconFont;

// ============================================================================
// #368: NodeBadgeSet bitmask unit tests
// ============================================================================

TEST(NodeBadgeSet, DefaultIsEmpty) {
    NodeBadgeSet set;
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.mask(), 0);
}

TEST(NodeBadgeSet, DefaultForEachInvokesNothing) {
    NodeBadgeSet set;
    int count = 0;
    set.for_each([&](NodeBadge) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(NodeBadgeSet, SingleSetAndTest) {
    NodeBadgeSet set;
    set.set(NodeBadge::Composite);
    EXPECT_TRUE(set.test(NodeBadge::Composite));
    EXPECT_FALSE(set.test(NodeBadge::Active));
    EXPECT_FALSE(set.test(NodeBadge::Locked));
    EXPECT_FALSE(set.test(NodeBadge::Warning));
    EXPECT_FALSE(set.test(NodeBadge::Error));
    EXPECT_FALSE(set.empty());
}

TEST(NodeBadgeSet, MultipleSet) {
    NodeBadgeSet set;
    set.set(NodeBadge::Composite);
    set.set(NodeBadge::Active);
    EXPECT_TRUE(set.test(NodeBadge::Composite));
    EXPECT_TRUE(set.test(NodeBadge::Active));
    EXPECT_FALSE(set.test(NodeBadge::Warning));
    EXPECT_FALSE(set.empty());
}

TEST(NodeBadgeSet, ClearRemovesBadge) {
    NodeBadgeSet set;
    set.set(NodeBadge::Warning);
    EXPECT_TRUE(set.test(NodeBadge::Warning));
    set.clear(NodeBadge::Warning);
    EXPECT_FALSE(set.test(NodeBadge::Warning));
    EXPECT_TRUE(set.empty());
}

TEST(NodeBadgeSet, ClearUnsetIsHarmless) {
    NodeBadgeSet set;
    set.clear(NodeBadge::Error);  // was never set
    EXPECT_TRUE(set.empty());
}

TEST(NodeBadgeSet, DoubleSetIsIdempotent) {
    NodeBadgeSet set;
    set.set(NodeBadge::Locked);
    set.set(NodeBadge::Locked);
    EXPECT_TRUE(set.test(NodeBadge::Locked));
    EXPECT_EQ(set.mask(), 1u << static_cast<uint8_t>(NodeBadge::Locked));
}

TEST(NodeBadgeSet, ForEachVisitsAllInOrder) {
    NodeBadgeSet set;
    set.set(NodeBadge::Error);     // index 4
    set.set(NodeBadge::Composite); // index 0
    set.set(NodeBadge::Locked);    // index 2

    std::vector<NodeBadge> visited;
    set.for_each([&](NodeBadge b) { visited.push_back(b); });

    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], NodeBadge::Composite);
    EXPECT_EQ(visited[1], NodeBadge::Locked);
    EXPECT_EQ(visited[2], NodeBadge::Error);
}

TEST(NodeBadgeSet, ForEachExactCount) {
    NodeBadgeSet set;
    set.set(NodeBadge::Active);
    set.set(NodeBadge::Warning);
    int count = 0;
    set.for_each([&](NodeBadge) { ++count; });
    EXPECT_EQ(count, 2);
}

TEST(NodeBadgeSet, AllBadges) {
    NodeBadgeSet set;
    set.set(NodeBadge::Composite);
    set.set(NodeBadge::Active);
    set.set(NodeBadge::Locked);
    set.set(NodeBadge::Warning);
    set.set(NodeBadge::Error);

    EXPECT_TRUE(set.test(NodeBadge::Composite));
    EXPECT_TRUE(set.test(NodeBadge::Active));
    EXPECT_TRUE(set.test(NodeBadge::Locked));
    EXPECT_TRUE(set.test(NodeBadge::Warning));
    EXPECT_TRUE(set.test(NodeBadge::Error));
    EXPECT_FALSE(set.empty());

    int count = 0;
    set.for_each([&](NodeBadge) { ++count; });
    EXPECT_EQ(count, 5);
}

TEST(NodeBadgeSet, MaskValuesAreCorrect) {
    // Each badge maps to its enum value as a bit position
    EXPECT_EQ(1u << 0, 1u << static_cast<uint8_t>(NodeBadge::Composite));
    EXPECT_EQ(1u << 1, 1u << static_cast<uint8_t>(NodeBadge::Active));
    EXPECT_EQ(1u << 2, 1u << static_cast<uint8_t>(NodeBadge::Locked));
    EXPECT_EQ(1u << 3, 1u << static_cast<uint8_t>(NodeBadge::Warning));
    EXPECT_EQ(1u << 4, 1u << static_cast<uint8_t>(NodeBadge::Error));

    NodeBadgeSet set;
    set.set(NodeBadge::Active);
    EXPECT_EQ(set.mask(), 0x02u);

    set.set(NodeBadge::Error);
    EXPECT_EQ(set.mask(), 0x12u);  // bit 1 + bit 4
}

TEST(NodeBadgeSet, ClearOneOfMultiplePreservesOthers) {
    NodeBadgeSet set;
    set.set(NodeBadge::Composite);
    set.set(NodeBadge::Active);
    set.set(NodeBadge::Error);

    set.clear(NodeBadge::Active);

    EXPECT_TRUE(set.test(NodeBadge::Composite));
    EXPECT_FALSE(set.test(NodeBadge::Active));
    EXPECT_TRUE(set.test(NodeBadge::Error));
}

// ============================================================================
// #369: BadgeVisuals lookup + codepoint_to_utf8 + IconFont::available
// ============================================================================

// --- BadgeVisuals ---

TEST(BadgeVisuals, CompositeCodepoint) {
    auto v = get_badge_visuals(NodeBadge::Composite);
    EXPECT_EQ(v.codepoint, 0xF126u);  // fa-code-branch
    EXPECT_NE(v.color, 0u);
}

TEST(BadgeVisuals, ActiveCodepoint) {
    auto v = get_badge_visuals(NodeBadge::Active);
    EXPECT_EQ(v.codepoint, 0xF0E7u);  // fa-bolt
    EXPECT_NE(v.color, 0u);
}

TEST(BadgeVisuals, LockedCodepoint) {
    auto v = get_badge_visuals(NodeBadge::Locked);
    EXPECT_EQ(v.codepoint, 0xF023u);  // fa-lock
    EXPECT_NE(v.color, 0u);
}

TEST(BadgeVisuals, WarningCodepoint) {
    auto v = get_badge_visuals(NodeBadge::Warning);
    EXPECT_EQ(v.codepoint, 0xF071u);  // fa-triangle-exclamation
    EXPECT_NE(v.color, 0u);
}

TEST(BadgeVisuals, ErrorCodepoint) {
    auto v = get_badge_visuals(NodeBadge::Error);
    EXPECT_EQ(v.codepoint, 0xF057u);  // fa-circle-xmark
    EXPECT_NE(v.color, 0u);
}

TEST(BadgeVisuals, AllColorsAreNonZero) {
    for (uint8_t i = 0; i < 5; ++i) {
        auto v = get_badge_visuals(static_cast<NodeBadge>(i));
        EXPECT_NE(v.color, 0u) << "Badge index " << (int)i << " has zero color";
    }
}

TEST(BadgeVisuals, OutOfRangeReturnsZeroed) {
    auto v = get_badge_visuals(static_cast<NodeBadge>(5));
    EXPECT_EQ(v.codepoint, 0u);
    EXPECT_EQ(v.color, 0u);
}

TEST(BadgeVisuals, MaxOutOfRangeReturnsZeroed) {
    auto v = get_badge_visuals(static_cast<NodeBadge>(255));
    EXPECT_EQ(v.codepoint, 0u);
    EXPECT_EQ(v.color, 0u);
}

TEST(BadgeVisuals, DistinctCodepointsForAllBadges) {
    // Each badge must have a unique codepoint (no accidental duplicates)
    uint32_t codepoints[5];
    for (uint8_t i = 0; i < 5; ++i) {
        codepoints[i] = get_badge_visuals(static_cast<NodeBadge>(i)).codepoint;
    }
    for (uint8_t i = 0; i < 5; ++i) {
        for (uint8_t j = i + 1; j < 5; ++j) {
            EXPECT_NE(codepoints[i], codepoints[j])
                << "Duplicate codepoint between badge " << (int)i << " and " << (int)j;
        }
    }
}

// --- codepoint_to_utf8 ---

TEST(CodepointToUtf8, AsciiSingleByte) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x41, out);  // 'A'
    EXPECT_EQ(n, 1);
    EXPECT_EQ(out[0], 'A');
    EXPECT_EQ(out[1], '\0');
}

TEST(CodepointToUtf8, AsciiZero) {
    char out[5] = {1, 1, 1, 1, 1};
    int n = IconFont::codepoint_to_utf8(0x00, out);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(out[0], '\0');
    // out[1] is the null terminator written by codepoint_to_utf8; out[2..4] untouched
}

TEST(CodepointToUtf8, AsciiBoundary0x7F) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x7F, out);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0x7F);
    EXPECT_EQ(out[1], '\0');
}

TEST(CodepointToUtf8, TwoByteBoundary0x80) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x80, out);
    EXPECT_EQ(n, 2);
    // 0x80 → 0xC2 0x80
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xC2);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x80);
    EXPECT_EQ(out[2], '\0');
}

TEST(CodepointToUtf8, TwoByteLatinEAcute) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x00E9, out);  // 'é'
    EXPECT_EQ(n, 2);
    // 0xE9 → 0xC3 0xA9
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xC3);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xA9);
    EXPECT_EQ(out[2], '\0');
}

TEST(CodepointToUtf8, TwoByteBoundary0x7FF) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x7FF, out);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xDF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xBF);
    EXPECT_EQ(out[2], '\0');
}

TEST(CodepointToUtf8, ThreeByteBoundary0x800) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x800, out);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xE0);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xA0);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0x80);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FontAwesomeComposite0xF126) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xF126, out);
    EXPECT_EQ(n, 3);
    // 0xF126 → 0xEF 0x84 0xA6
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x84);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0xA6);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FontAwesomeActive0xF0E7) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xF0E7, out);
    EXPECT_EQ(n, 3);
    // 0xF0E7 → 0xEF 0x83 0xA7
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x83);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0xA7);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FontAwesomeLocked0xF023) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xF023, out);
    EXPECT_EQ(n, 3);
    // 0xF023 → 0xEF 0x80 0xA3
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x80);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0xA3);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FontAwesomeWarning0xF071) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xF071, out);
    EXPECT_EQ(n, 3);
    // 0xF071 → 0xEF 0x81 0xB1
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x81);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0xB1);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FontAwesomeError0xF057) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xF057, out);
    EXPECT_EQ(n, 3);
    // 0xF057 → 0xEF 0x81 0x97
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x81);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0x97);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, ThreeByteBoundary0xFFFF) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0xFFFF, out);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xEF);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xBF);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0xBF);
    EXPECT_EQ(out[3], '\0');
}

TEST(CodepointToUtf8, FourByteBoundary0x10000) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x10000, out);
    EXPECT_EQ(n, 4);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xF0);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x90);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0x80);
    EXPECT_EQ(static_cast<uint8_t>(out[3]), 0x80);
    EXPECT_EQ(out[4], '\0');
}

TEST(CodepointToUtf8, FourByteEmoji0x1F600) {
    char out[5] = {};
    int n = IconFont::codepoint_to_utf8(0x1F600, out);  // 😀
    EXPECT_EQ(n, 4);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xF0);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0x9F);
    EXPECT_EQ(static_cast<uint8_t>(out[2]), 0x98);
    EXPECT_EQ(static_cast<uint8_t>(out[3]), 0x80);
    EXPECT_EQ(out[4], '\0');
}

TEST(CodepointToUtf8, NullTerminatorAlwaysPresent) {
    // Verify null terminator for all 4 encoding widths
    for (uint32_t cp : {0x00u, 0x7Fu, 0x80u, 0x7FFu, 0x800u, 0xFFFFu, 0x10000u, 0x10FFFFu}) {
        char out[5] = {1, 1, 1, 1, 1};  // sentinel fill
        int n = IconFont::codepoint_to_utf8(cp, out);
        ASSERT_GE(n, 1);
        ASSERT_LE(n, 4);
        EXPECT_EQ(out[n], '\0')
            << "Null terminator missing after " << n << " bytes for cp=0x"
            << std::hex << cp << std::dec;
    }
}

TEST(CodepointToUtf8, AllFontAwesomeCodepointsAreThreeBytes) {
    // All FA codepoints used are in the BMP 3-byte range
    for (uint8_t i = 0; i < 5; ++i) {
        auto v = get_badge_visuals(static_cast<NodeBadge>(i));
        char out[5] = {};
        int n = IconFont::codepoint_to_utf8(v.codepoint, out);
        EXPECT_EQ(n, 3) << "Badge " << (int)i << " codepoint 0x"
                        << std::hex << v.codepoint << std::dec
                        << " should encode as 3 UTF-8 bytes";
    }
}

// --- IconFont::available() ---

TEST(IconFont, NullHandleNotAvailable) {
    IconFont font;
    EXPECT_EQ(font.handle, 0u);
    EXPECT_FALSE(font.available());
}

TEST(IconFont, NonNullHandleIsAvailable) {
    IconFont font;
    font.handle = static_cast<ui::IDrawList::NativeFont>(0xDEADBEEF);  // non-null sentinel
    EXPECT_TRUE(font.available());
}
