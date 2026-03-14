#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "editor/data/port.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/data/blueprint.h"

/// TDD: Failing tests that define the InternedId migration contract.
/// These tests verify that data types use InternedId instead of std::string
/// for their identity fields, and that Blueprint owns the StringInterner.

using ui::InternedId;
using ui::StringInterner;

// =============================================================================
// Blueprint owns a StringInterner
// =============================================================================

TEST(BlueprintInterning, HasInterner) {
    Blueprint bp;
    // Blueprint must expose a StringInterner for interning IDs
    StringInterner& interner = bp.interner();
    EXPECT_EQ(interner.size(), 0u);
}

TEST(BlueprintInterning, InternAndResolve) {
    Blueprint bp;
    auto id = bp.interner().intern("battery_1");
    EXPECT_EQ(bp.interner().resolve(id), "battery_1");
}

// =============================================================================
// Node::id is InternedId
// =============================================================================

TEST(NodeInterning, IdIsInternedId) {
    // Node::id should be InternedId, not std::string
    static_assert(std::is_same_v<decltype(Node::id), InternedId>,
                  "Node::id must be InternedId");
}

TEST(NodeInterning, DefaultNodeIdIsEmpty) {
    Node n;
    EXPECT_TRUE(n.id.empty());
}

TEST(NodeInterning, AddNodeWithInternedId) {
    Blueprint bp;
    Node n;
    n.id = bp.interner().intern("bat1");
    n.name = "Battery";
    n.type_name = "Battery";
    bp.add_node(std::move(n));

    EXPECT_EQ(bp.nodes.size(), 1u);
    EXPECT_EQ(bp.nodes[0].id, bp.interner().intern("bat1"));
    EXPECT_EQ(bp.interner().resolve(bp.nodes[0].id), "bat1");
}

TEST(NodeInterning, FindNodeByInternedId) {
    Blueprint bp;
    auto id = bp.interner().intern("pump1");
    Node n;
    n.id = id;
    n.type_name = "Pump";
    bp.add_node(std::move(n));

    // find_node should accept InternedId
    Node* found = bp.find_node(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, id);

    // Not found case
    auto missing = bp.interner().intern("nonexistent");
    EXPECT_EQ(bp.find_node(missing), nullptr);
}

TEST(NodeInterning, FindNodeByStringStillWorks) {
    // Convenience overload: find_node(const char*) still works
    // by interning the string on-the-fly
    Blueprint bp;
    Node n;
    n.id = bp.interner().intern("lamp1");
    n.type_name = "Lamp";
    bp.add_node(std::move(n));

    Node* found = bp.find_node("lamp1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(bp.interner().resolve(found->id), "lamp1");
}

TEST(NodeInterning, NodeIndexUsesInternedId) {
    Blueprint bp;
    auto id = bp.interner().intern("bus1");
    Node n;
    n.id = id;
    n.type_name = "Bus";
    bp.add_node(std::move(n));

    // node_index_ should be keyed by InternedId
    static_assert(
        std::is_same_v<decltype(bp.node_index_)::key_type, InternedId>,
        "node_index_ must be keyed by InternedId");

    EXPECT_EQ(bp.node_index_.count(id), 1u);
}

TEST(NodeInterning, DuplicateNodeIdReturnsExistingIndex) {
    Blueprint bp;
    auto id = bp.interner().intern("dup_node");

    Node n1;
    n1.id = id;
    n1.type_name = "TypeA";
    size_t idx1 = bp.add_node(std::move(n1));

    Node n2;
    n2.id = id;
    n2.type_name = "TypeB";
    size_t idx2 = bp.add_node(std::move(n2));

    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(bp.nodes.size(), 1u);
}

// =============================================================================
// WireEnd uses InternedId
// =============================================================================

TEST(WireEndInterning, FieldsAreInternedId) {
    static_assert(std::is_same_v<decltype(WireEnd::node_id), InternedId>,
                  "WireEnd::node_id must be InternedId");
    static_assert(std::is_same_v<decltype(WireEnd::port_name), InternedId>,
                  "WireEnd::port_name must be InternedId");
}

TEST(WireEndInterning, DefaultIsEmpty) {
    WireEnd we;
    EXPECT_TRUE(we.node_id.empty());
    EXPECT_TRUE(we.port_name.empty());
}

TEST(WireEndInterning, ConstructWithInternedIds) {
    StringInterner interner;
    auto node = interner.intern("bat1");
    auto port = interner.intern("v_out");
    WireEnd we(node, port, PortSide::Output);
    EXPECT_EQ(we.node_id, node);
    EXPECT_EQ(we.port_name, port);
    EXPECT_EQ(we.side, PortSide::Output);
}

// =============================================================================
// Wire::id is InternedId
// =============================================================================

TEST(WireInterning, IdIsInternedId) {
    static_assert(std::is_same_v<decltype(Wire::id), InternedId>,
                  "Wire::id must be InternedId");
}

TEST(WireInterning, DefaultWireIdIsEmpty) {
    Wire w;
    EXPECT_TRUE(w.id.empty());
}

TEST(WireInterning, WireMakeWithInternedIds) {
    StringInterner interner;
    auto wid = interner.intern("w1");
    auto n1 = interner.intern("bat1");
    auto p1 = interner.intern("v_out");
    auto n2 = interner.intern("load1");
    auto p2 = interner.intern("v_in");

    Wire w = Wire::make(wid,
        WireEnd(n1, p1, PortSide::Output),
        WireEnd(n2, p2, PortSide::Input));

    EXPECT_EQ(w.id, wid);
    EXPECT_EQ(w.start.node_id, n1);
    EXPECT_EQ(w.start.port_name, p1);
    EXPECT_EQ(w.end.node_id, n2);
    EXPECT_EQ(w.end.port_name, p2);
}

// =============================================================================
// WireKey uses InternedId (trivial equality + hashing)
// =============================================================================

TEST(WireKeyInterning, FieldsAreInternedId) {
    static_assert(std::is_same_v<decltype(WireKey::start_node), InternedId>,
                  "WireKey::start_node must be InternedId");
    static_assert(std::is_same_v<decltype(WireKey::start_port), InternedId>,
                  "WireKey::start_port must be InternedId");
    static_assert(std::is_same_v<decltype(WireKey::end_node), InternedId>,
                  "WireKey::end_node must be InternedId");
    static_assert(std::is_same_v<decltype(WireKey::end_port), InternedId>,
                  "WireKey::end_port must be InternedId");
}

TEST(WireKeyInterning, EqualityIsTrivialIntComparison) {
    StringInterner interner;
    auto n1 = interner.intern("bat1");
    auto p1 = interner.intern("v_out");
    auto n2 = interner.intern("load1");
    auto p2 = interner.intern("v_in");

    Wire w = Wire::make(interner.intern("w1"),
        WireEnd(n1, p1, PortSide::Output),
        WireEnd(n2, p2, PortSide::Input));

    WireKey a(w);
    WireKey b(w);
    EXPECT_EQ(a, b);  // should be trivial uint32 comparison, no string work
}

TEST(WireKeyInterning, HashIsTrivialIntHash) {
    StringInterner interner;
    Wire w = Wire::make(interner.intern("w1"),
        WireEnd(interner.intern("n1"), interner.intern("p1"), PortSide::Output),
        WireEnd(interner.intern("n2"), interner.intern("p2"), PortSide::Input));

    WireKey key(w);
    WireKeyHash hasher;
    size_t h = hasher(key);
    // Just verify it's deterministic
    EXPECT_EQ(h, hasher(key));
}

TEST(WireKeyInterning, SizeIs16Bytes) {
    // 4 InternedId fields × 4 bytes = 16 bytes (down from ~128 bytes with strings)
    static_assert(sizeof(WireKey) == 4 * sizeof(InternedId),
                  "WireKey should be exactly 16 bytes (4 InternedId fields)");
}

// =============================================================================
// PortKey uses InternedId
// =============================================================================

TEST(PortKeyInterning, FieldsAreInternedId) {
    static_assert(std::is_same_v<decltype(PortKey::node_id), InternedId>,
                  "PortKey::node_id must be InternedId");
    static_assert(std::is_same_v<decltype(PortKey::port_name), InternedId>,
                  "PortKey::port_name must be InternedId");
}

TEST(PortKeyInterning, EqualityIsTrivialIntComparison) {
    StringInterner interner;
    auto nid = interner.intern("bus1");
    auto pname = interner.intern("v_in");

    PortKey a(nid, pname);
    PortKey b(nid, pname);
    EXPECT_EQ(a, b);
}

TEST(PortKeyInterning, SizeIs8Bytes) {
    // 2 InternedId fields × 4 bytes = 8 bytes (down from ~64 bytes with strings)
    static_assert(sizeof(PortKey) == 2 * sizeof(InternedId),
                  "PortKey should be exactly 8 bytes (2 InternedId fields)");
}

// =============================================================================
// EditorPort::name is InternedId
// =============================================================================

TEST(EditorPortInterning, NameIsInternedId) {
    static_assert(std::is_same_v<decltype(EditorPort::name), InternedId>,
                  "EditorPort::name must be InternedId");
}

TEST(EditorPortInterning, DefaultNameIsEmpty) {
    EditorPort p;
    EXPECT_TRUE(p.name.empty());
}

TEST(EditorPortInterning, ConstructWithInternedId) {
    StringInterner interner;
    auto name = interner.intern("v_in");
    EditorPort p(name, PortSide::Input, PortType::V);
    EXPECT_EQ(p.name, name);
    EXPECT_EQ(p.side, PortSide::Input);
    EXPECT_EQ(p.type, PortType::V);
}

// =============================================================================
// Blueprint wire indices use InternedId
// =============================================================================

TEST(BlueprintInterning, WireIdIndexUsesInternedId) {
    Blueprint bp;
    static_assert(
        std::is_same_v<decltype(bp.wire_id_index_)::key_type, InternedId>,
        "wire_id_index_ must be keyed by InternedId");
}

TEST(BlueprintInterning, AddWireWithInternedIds) {
    Blueprint bp;
    auto& interner = bp.interner();

    Node n1;
    n1.id = interner.intern("n1");
    n1.type_name = "Battery";
    n1.output(interner.intern("v_out"));
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = interner.intern("n2");
    n2.type_name = "Load";
    n2.input(interner.intern("v_in"));
    bp.add_node(std::move(n2));

    Wire w;
    w.id = interner.intern("w1");
    w.start = WireEnd(interner.intern("n1"), interner.intern("v_out"), PortSide::Output);
    w.end = WireEnd(interner.intern("n2"), interner.intern("v_in"), PortSide::Input);
    bp.add_wire(std::move(w));

    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wires[0].start.node_id, interner.intern("n1"));
}

TEST(BlueprintInterning, FindWireByInternedId) {
    Blueprint bp;
    auto& interner = bp.interner();

    Node n1;
    n1.id = interner.intern("n1");
    bp.add_node(std::move(n1));
    Node n2;
    n2.id = interner.intern("n2");
    bp.add_node(std::move(n2));

    Wire w;
    w.id = interner.intern("w1");
    w.start = WireEnd(interner.intern("n1"), interner.intern("out"), PortSide::Output);
    w.end = WireEnd(interner.intern("n2"), interner.intern("in"), PortSide::Input);
    bp.add_wire(std::move(w));

    // find_wire should accept InternedId
    auto wid = interner.intern("w1");
    Wire* found = bp.find_wire(wid);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, wid);

    auto missing = interner.intern("w999");
    EXPECT_EQ(bp.find_wire(missing), nullptr);
}

TEST(BlueprintInterning, BusWireIndexUsesInternedId) {
    Blueprint bp;
    // bus_wire_index_ should be keyed by InternedId,
    // values should be vectors of InternedId
    static_assert(
        std::is_same_v<decltype(bp.bus_wire_index_)::key_type, InternedId>,
        "bus_wire_index_ keys must be InternedId");
}

TEST(BlueprintInterning, PortOccupancyIndexUsesInternedIdKeys) {
    Blueprint bp;
    // port_occupancy_index_ should use PortKey (with InternedId fields)
    // and values should be sets of InternedId
    auto& occ = bp.port_occupancy_index_;
    using ValueType = std::decay_t<decltype(occ)>::mapped_type;
    static_assert(
        std::is_same_v<ValueType, std::unordered_set<InternedId>>,
        "port_occupancy_index_ values must be unordered_set<InternedId>");
}

// =============================================================================
// Integration: full add + find + remove cycle with InternedIds
// =============================================================================

TEST(BlueprintInterning, FullCycleAddFindRemove) {
    Blueprint bp;
    auto& interner = bp.interner();

    // Add nodes
    for (const char* name : {"bat1", "bus1", "load1"}) {
        Node n;
        n.id = interner.intern(name);
        n.type_name = name;
        bp.add_node(std::move(n));
    }

    // Add wire
    Wire w;
    w.id = interner.intern("w1");
    w.start = WireEnd(interner.intern("bat1"), interner.intern("v_out"), PortSide::Output);
    w.end = WireEnd(interner.intern("bus1"), interner.intern("v_in"), PortSide::Input);
    bp.add_wire(std::move(w));

    EXPECT_EQ(bp.nodes.size(), 3u);
    EXPECT_EQ(bp.wires.size(), 1u);

    // Find
    EXPECT_NE(bp.find_node(interner.intern("bat1")), nullptr);
    EXPECT_NE(bp.find_wire(interner.intern("w1")), nullptr);

    // Remove wire
    Wire removed = bp.remove_wire_at(0);
    EXPECT_EQ(removed.id, interner.intern("w1"));
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(bp.find_wire(interner.intern("w1")), nullptr);
}

// =============================================================================
// Wire dedup index works with InternedId-based WireKey
// =============================================================================

TEST(BlueprintInterning, WireDedupPreventsDuplicateWires) {
    Blueprint bp;
    auto& interner = bp.interner();

    Node n1;
    n1.id = interner.intern("n1");
    bp.add_node(std::move(n1));
    Node n2;
    n2.id = interner.intern("n2");
    bp.add_node(std::move(n2));

    auto make_wire = [&](const char* wid) {
        Wire w;
        w.id = interner.intern(wid);
        w.start = WireEnd(interner.intern("n1"), interner.intern("out"), PortSide::Output);
        w.end = WireEnd(interner.intern("n2"), interner.intern("in"), PortSide::Input);
        return w;
    };

    bp.add_wire(make_wire("w1"));
    bp.add_wire(make_wire("w2"));  // same endpoints, different ID — should be deduped

    // Only one wire should exist (dedup by endpoints)
    EXPECT_EQ(bp.wire_index_size(), 1u);
}

// =============================================================================
// Port occupancy works with InternedId
// =============================================================================

TEST(BlueprintInterning, PortOccupancyWithInternedId) {
    Blueprint bp;
    auto& interner = bp.interner();

    Node n1;
    n1.id = interner.intern("n1");
    bp.add_node(std::move(n1));
    Node n2;
    n2.id = interner.intern("n2");
    bp.add_node(std::move(n2));

    Wire w;
    w.id = interner.intern("w1");
    w.start = WireEnd(interner.intern("n1"), interner.intern("out"), PortSide::Output);
    w.end = WireEnd(interner.intern("n2"), interner.intern("in"), PortSide::Input);
    bp.add_wire(std::move(w));

    // is_port_occupied should accept InternedId
    EXPECT_TRUE(bp.is_port_occupied(interner.intern("n1"), interner.intern("out")));
    EXPECT_TRUE(bp.is_port_occupied(interner.intern("n2"), interner.intern("in")));
    EXPECT_FALSE(bp.is_port_occupied(interner.intern("n1"), interner.intern("in")));
}
