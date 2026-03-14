#include <gtest/gtest.h>
#include "editor/window/properties_window.h"
#include "editor/data/node.h"
#include "ui/core/interned_id.h"

// Allow gtest to print InternedId values on assertion failure
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}

static ui::StringInterner g_interner;

// =============================================================================
// Phase 2: PropertiesWindow Tests — TDD
// =============================================================================

TEST(PropertiesWindow, OpenSetsTarget) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};

    PropertiesWindow win;
    EXPECT_FALSE(win.isOpen());

    win.open(n, "bat1", [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());
    EXPECT_EQ(win.targetNodeId(), "bat1");
}

TEST(PropertiesWindow, CancelRevertsParams) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}, {"r", "0.01"}};

    PropertiesWindow win;
    win.open(n, "bat1", [](const std::string&) {});

    // Simulate user editing params directly
    n.params["v"] = "12.0";
    n.name = "modified_name";

    // Cancel should revert
    win.close();

    EXPECT_EQ(n.params["v"], "28.0") << "Cancel must revert params";
    EXPECT_EQ(n.params["r"], "0.01") << "Untouched params preserved";
    EXPECT_EQ(n.name, "bat1") << "Cancel must revert name";
    EXPECT_FALSE(win.isOpen());
}

TEST(PropertiesWindow, CancelRevertsAddedParam) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};

    PropertiesWindow win;
    win.open(n, "bat1", [](const std::string&) {});

    // User adds a param that wasn't in the original
    n.params["new_key"] = "new_value";

    win.close();

    // snapshot_params_ had only {"v": "28.0"}, so restore should remove "new_key"
    EXPECT_EQ(n.params.size(), 1u);
    EXPECT_EQ(n.params.at("v"), "28.0");
    EXPECT_EQ(n.params.count("new_key"), 0u)
        << "Added param should be removed on cancel";
}

TEST(PropertiesWindow, OpenTwiceOverwritesSnapshot) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.name = "bat1";
    n.params = {{"v", "28.0"}};

    PropertiesWindow win;

    // First open
    win.open(n, "bat1", [](const std::string&) {});

    // User edits
    n.params["v"] = "12.0";

    // Open again (re-snapshot)
    win.open(n, "bat1", [](const std::string&) {});

    // Cancel now should revert to the second snapshot (v=12.0)
    win.close();
    EXPECT_EQ(n.params["v"], "12.0")
        << "Second open() should create a new snapshot";
}

TEST(PropertiesWindow, ClosedWindowIsNotOpen) {
    Node n;
    n.id = g_interner.intern("bat1");
    n.params = {};

    PropertiesWindow win;
    win.open(n, "bat1", [](const std::string&) {});
    EXPECT_TRUE(win.isOpen());

    win.close();
    EXPECT_FALSE(win.isOpen());
}
