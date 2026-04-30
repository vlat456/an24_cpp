#include "simconnect/wire_protocol.h"
#include "simconnect/intern_table.h"
#include "simconnect/wire_codec.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <cmath>

// =============================================================================
// Wire Protocol V2 Types — #445
// =============================================================================

TEST(WireProtocolTest, PacketHeaderSizeIs8Bytes) {
    EXPECT_EQ(sizeof(PacketHeader), 8u);
}

TEST(WireProtocolTest, VarRecordSizeIs8Bytes) {
    EXPECT_EQ(sizeof(VarRecord), 8u);
}

TEST(WireProtocolTest, WireValueSizeIs4Bytes) {
    EXPECT_EQ(sizeof(WireValue), 4u);
}

TEST(WireProtocolTest, PacketHeaderDefaultInit) {
    PacketHeader hdr;
    EXPECT_EQ(hdr.magic, PACKET_MAGIC);
    EXPECT_EQ(hdr.version, PROTOCOL_VERSION);
    EXPECT_EQ(hdr.cmd, 0u);
    EXPECT_EQ(hdr.count, 0u);
    EXPECT_EQ(hdr.seq_id, 0u);
}

TEST(WireProtocolTest, VarRecordDefaultInit) {
    VarRecord rec;
    EXPECT_EQ(rec.var_type, VarType::AVar);
    EXPECT_EQ(rec.val_type, ValType::Float32);
    EXPECT_EQ(rec.name_id, 0u);
    EXPECT_EQ(rec.value.u32, 0u);
}

TEST(WireProtocolTest, WireValueConstructors) {
    WireValue vf = WireValue(3.14f);
    EXPECT_FLOAT_EQ(vf.f32, 3.14f);

    WireValue vi = WireValue(int32_t(-42));
    EXPECT_EQ(vi.i32, -42);

    WireValue vu = WireValue(uint32_t(100));
    EXPECT_EQ(vu.u32, 100u);

    WireValue vb = WireValue(true);
    EXPECT_EQ(vb.b, true);
}

TEST(WireProtocolTest, WireValueBoolIsDeterministicOnWire) {
    // Regression: WireValue(bool) must write all 4 bytes deterministically.
    WireValue v_true(true);
    EXPECT_EQ(v_true.u32, 1u);

    WireValue v_false(false);
    EXPECT_EQ(v_false.u32, 0u);

    // Verify wire bytes are fully deterministic
    VarRecord rec;
    rec.var_type = VarType::AVar;
    rec.val_type = ValType::Bool;
    rec.name_id  = 42;
    rec.value    = WireValue(true);

    uint8_t wire[8];
    std::memcpy(wire, &rec, sizeof(VarRecord));

    EXPECT_EQ(wire[4], 0x01);
    EXPECT_EQ(wire[5], 0x00);
    EXPECT_EQ(wire[6], 0x00);
    EXPECT_EQ(wire[7], 0x00);
}

TEST(WireProtocolTest, VarRecordFieldOffsets) {
    EXPECT_EQ(offsetof(VarRecord, var_type), 0u);
    EXPECT_EQ(offsetof(VarRecord, val_type), 1u);
    EXPECT_EQ(offsetof(VarRecord, name_id), 2u);
    EXPECT_EQ(offsetof(VarRecord, value), 4u);
}

// =============================================================================
// V2 Cmd enum — stable wire values
// =============================================================================

TEST(WireProtocolTest, CmdEnumValuesAreStable) {
    // V2 delta protocol — wire values must never change
    EXPECT_EQ(static_cast<uint8_t>(Cmd::DeltaRead),   0x01);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::DeltaUpdate), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FullSync),    0x03);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::DeltaWrite),  0x04);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::WriteAck),    0x05);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RegisterNames),     0x10);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RegisterNamesResp), 0x11);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Ping), 0xF0);
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Pong), 0xF1);
}

TEST(WireProtocolTest, VarTypeEnumValuesAreStable) {
    EXPECT_EQ(static_cast<uint8_t>(VarType::AVar), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(VarType::LVar), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(VarType::HEvent), 0x03);
    EXPECT_EQ(static_cast<uint8_t>(VarType::BVar), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(VarType::EVar), 0x05);
    EXPECT_EQ(static_cast<uint8_t>(VarType::IVar), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(VarType::OVar), 0x07);
    EXPECT_EQ(static_cast<uint8_t>(VarType::ZVar), 0x08);
}

TEST(WireProtocolTest, VarTypeNameRoundTrip) {
    const VarType types[] = {
        VarType::AVar, VarType::LVar, VarType::HEvent, VarType::BVar,
        VarType::EVar, VarType::IVar, VarType::OVar, VarType::ZVar
    };
    for (auto t : types) {
        auto name = var_type_name(t);
        VarType parsed;
        EXPECT_TRUE(parse_var_type(name, parsed));
        EXPECT_EQ(parsed, t);
    }
}

TEST(WireProtocolTest, ParseVarTypeRejectsUnknown) {
    VarType out;
    EXPECT_FALSE(parse_var_type("NotARealType", out));
    EXPECT_FALSE(parse_var_type("", out));
}

// =============================================================================
// V2 Protocol constants
// =============================================================================

TEST(WireProtocolTest, ProtocolVersionIs2) {
    EXPECT_EQ(PROTOCOL_VERSION, 2u);
}

TEST(WireProtocolTest, TierMaskConstants) {
    EXPECT_EQ(TIER_MASK_FAST, 0x01u);
    EXPECT_EQ(TIER_MASK_MEDIUM, 0x02u);
    EXPECT_EQ(TIER_MASK_SLOW, 0x04u);
    EXPECT_EQ(TIER_MASK_ALL, 0x07u);
    EXPECT_EQ(TIER_MASK_FORCE_FULL_SYNC, 0xFFFFu);
}

TEST(WireProtocolTest, FullSyncIntervalIs60) {
    EXPECT_EQ(FULL_SYNC_INTERVAL, 60u);
}

TEST(WireProtocolTest, TierMaskForFrame) {
    // Frame 0: 0%5==0, 0%30==0 → all three tiers
    EXPECT_EQ(tier_mask_for_frame(0), TIER_MASK_FAST | TIER_MASK_MEDIUM | TIER_MASK_SLOW);
    // Frame 1: fast only
    EXPECT_EQ(tier_mask_for_frame(1), TIER_MASK_FAST);
    // Frame 4: fast only
    EXPECT_EQ(tier_mask_for_frame(4), TIER_MASK_FAST);
    // Frame 5: fast + medium
    EXPECT_EQ(tier_mask_for_frame(5), TIER_MASK_FAST | TIER_MASK_MEDIUM);
    // Frame 10: fast + medium
    EXPECT_EQ(tier_mask_for_frame(10), TIER_MASK_FAST | TIER_MASK_MEDIUM);
    // Frame 30: fast + medium + slow (30%5==0, 30%30==0)
    EXPECT_EQ(tier_mask_for_frame(30), TIER_MASK_FAST | TIER_MASK_MEDIUM | TIER_MASK_SLOW);
    // Frame 60: same as frame 0 — all tiers
    EXPECT_EQ(tier_mask_for_frame(60), TIER_MASK_FAST | TIER_MASK_MEDIUM | TIER_MASK_SLOW);
    // Frame 31: fast only (31%5!=0, 31%30!=0)
    EXPECT_EQ(tier_mask_for_frame(31), TIER_MASK_FAST);
}

// =============================================================================
// Default epsilon per VarType
// =============================================================================

TEST(WireProtocolTest, DefaultEpsilonByVarType) {
    EXPECT_FLOAT_EQ(default_epsilon(VarType::EVar), 0.5f);
    EXPECT_FLOAT_EQ(default_epsilon(VarType::AVar), 0.01f);
    EXPECT_FLOAT_EQ(default_epsilon(VarType::LVar), 0.005f);
    EXPECT_FLOAT_EQ(default_epsilon(VarType::BVar), 0.005f);
    EXPECT_FLOAT_EQ(default_epsilon(VarType::HEvent), 0.0f);
}

// =============================================================================
// Default tier per VarType
// =============================================================================

TEST(WireProtocolTest, DefaultTierByVarType) {
    EXPECT_EQ(default_tier(VarType::AVar), TIER_FAST);
    EXPECT_EQ(default_tier(VarType::EVar), TIER_SLOW);
    EXPECT_EQ(default_tier(VarType::HEvent), TIER_FAST);
    EXPECT_EQ(default_tier(VarType::LVar), TIER_MEDIUM);
}

// =============================================================================
// Change detection — value_changed()
// =============================================================================

TEST(WireProtocolTest, ValueChangedFloatAboveEpsilon) {
    WireValue prev(28.0f);
    WireValue curr(28.5f);
    EXPECT_TRUE(value_changed(curr, prev, 0.01f, ValType::Float32));
}

TEST(WireProtocolTest, ValueChangedFloatBelowEpsilon) {
    WireValue prev(28.0f);
    WireValue curr(28.005f);
    EXPECT_FALSE(value_changed(curr, prev, 0.01f, ValType::Float32));
}

TEST(WireProtocolTest, ValueChangedFloatExactEpsilon) {
    WireValue prev(28.0f);
    WireValue curr(28.0f);
    EXPECT_FALSE(value_changed(curr, prev, 0.01f, ValType::Float32));
}

TEST(WireProtocolTest, ValueChangedFloatLargeValueRelativeThreshold) {
    // Large values: threshold scales with magnitude
    WireValue prev(10000.0f);
    WireValue curr(10050.0f);
    // epsilon=0.01 → threshold = max(0.01, 10000*0.01) = 100.0
    // diff = 50 < 100 → no change
    EXPECT_FALSE(value_changed(curr, prev, 0.01f, ValType::Float32));

    // But 200 diff > 100 threshold → changed
    WireValue curr2(10200.0f);
    EXPECT_TRUE(value_changed(curr2, prev, 0.01f, ValType::Float32));
}

TEST(WireProtocolTest, ValueChangedBoolExactMatch) {
    WireValue t(true), f(false);
    EXPECT_TRUE(value_changed(t, f, 0.0f, ValType::Bool));
    EXPECT_FALSE(value_changed(t, t, 0.0f, ValType::Bool));
    EXPECT_FALSE(value_changed(f, f, 0.0f, ValType::Bool));
}

TEST(WireProtocolTest, ValueChangedInt32ExactMatch) {
    WireValue a(int32_t(42)), b(int32_t(42)), c(int32_t(43));
    EXPECT_FALSE(value_changed(a, b, 0.0f, ValType::Int32));
    EXPECT_TRUE(value_changed(a, c, 0.0f, ValType::Int32));
}

TEST(WireProtocolTest, ValueChangedDetectsNaN) {
    // NaN has distinct bit pattern from 0.0f — u32 comparison catches it
    WireValue normal(0.0f);
    WireValue nan_val;
    nan_val.f32 = std::nanf("");
    // NaN's u32 bit pattern differs from 0.0f's u32
    EXPECT_NE(nan_val.u32, normal.u32);
    // With zero epsilon, value_changed uses exact u32 comparison
    EXPECT_TRUE(value_changed(nan_val, normal, 0.0f, ValType::Float32));
}

TEST(WireProtocolTest, ValueChangedZeroEpsilonUsesExactMatch) {
    WireValue prev(1.0f);
    WireValue curr(1.000001f);  // Tiny difference
    EXPECT_TRUE(value_changed(curr, prev, 0.0f, ValType::Float32));
}

// =============================================================================
// Inline helpers
// =============================================================================

TEST(WireProtocolTest, IsValidHeaderRejectsBadMagic) {
    uint8_t buf[8] = {0xFF, 0xFF, 0x02, 0x01, 0, 0, 0, 0};
    EXPECT_FALSE(is_valid_header(buf, 8));
}

TEST(WireProtocolTest, IsValidHeaderRejectsShortBuffer) {
    uint8_t buf[4] = {};
    EXPECT_FALSE(is_valid_header(buf, 4));
}

TEST(WireProtocolTest, PacketSizeCalculation) {
    EXPECT_EQ(packet_size(0), 8u);    // Header only
    EXPECT_EQ(packet_size(1), 16u);   // Header + 1 record
    EXPECT_EQ(packet_size(50), 408u); // Header + 50 records
}

// =============================================================================
// InternTable — unchanged from V1, still valid
// =============================================================================

TEST(InternTableTest, Fnv1aHashDeterministic) {
    auto h1 = fnv1a_hash("ELECTRICAL MAIN BUS VOLTAGE");
    auto h2 = fnv1a_hash("ELECTRICAL MAIN BUS VOLTAGE");
    EXPECT_EQ(h1, h2);

    auto h3 = fnv1a_hash("AMBIENT TEMPERATURE");
    EXPECT_NE(h1, h3);
}

TEST(InternTableTest, ComputeInternIdDeterministic) {
    auto id1 = compute_intern_id("GENERAL ENG RPM:1");
    auto id2 = compute_intern_id("GENERAL ENG RPM:1");
    EXPECT_EQ(id1, id2);

    auto id3 = compute_intern_id("AMBIENT PRESSURE");
    EXPECT_NE(id1, id3);
}

TEST(InternTableTest, InternReturnsConsistentId) {
    InternTable table;
    uint16_t id1 = table.intern(VarType::AVar, "TEST_VAR");
    uint16_t id2 = table.intern(VarType::AVar, "TEST_VAR");
    EXPECT_EQ(id1, id2);
}

TEST(InternTableTest, InternDifferentNamesGivesDifferentIds) {
    InternTable table;
    uint16_t id1 = table.intern(VarType::AVar, "VAR_A");
    uint16_t id2 = table.intern(VarType::AVar, "VAR_B");
    EXPECT_NE(id1, id2);
}

TEST(InternTableTest, LookupFindsInterned) {
    InternTable table;
    uint16_t id = table.intern(VarType::LVar, "MY_LVAR");
    auto found = table.lookup("MY_LVAR");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, id);
}

TEST(InternTableTest, LookupMissReturnsNullopt) {
    InternTable table;
    EXPECT_FALSE(table.lookup("NOT_INTERNED").has_value());
}

TEST(InternTableTest, NameByIdRoundTrip) {
    InternTable table;
    uint16_t id = table.intern(VarType::AVar, "ELECTRICAL MAIN BUS VOLTAGE");
    auto name = table.name_by_id(id);
    EXPECT_EQ(name, "ELECTRICAL MAIN BUS VOLTAGE");
}

TEST(InternTableTest, NameByIdReturnsEmptyForUnknown) {
    InternTable table;
    EXPECT_TRUE(table.name_by_id(9999).empty());
}

TEST(InternTableTest, EntryByIdReturnsCorrectVarType) {
    InternTable table;
    table.intern(VarType::AVar, "AVAR_1");
    table.intern(VarType::LVar, "LVAR_1");
    table.intern(VarType::HEvent, "HEVENT_1");

    auto* e1 = table.entry_by_id(*table.lookup("AVAR_1"));
    ASSERT_NE(e1, nullptr);
    EXPECT_EQ(e1->var_type, VarType::AVar);

    auto* e2 = table.entry_by_id(*table.lookup("LVAR_1"));
    ASSERT_NE(e2, nullptr);
    EXPECT_EQ(e2->var_type, VarType::LVar);

    auto* e3 = table.entry_by_id(*table.lookup("HEVENT_1"));
    ASSERT_NE(e3, nullptr);
    EXPECT_EQ(e3->var_type, VarType::HEvent);
}

TEST(InternTableTest, ClearResetsState) {
    InternTable table;
    table.intern(VarType::AVar, "VAR");
    EXPECT_EQ(table.size(), 1u);
    EXPECT_TRUE(table.lookup("VAR").has_value());

    table.clear();
    EXPECT_EQ(table.size(), 0u);
    EXPECT_FALSE(table.lookup("VAR").has_value());
}

TEST(InternTableTest, ConsistentAcrossMultipleInternTableInstances) {
    InternTable t1, t2;
    uint16_t id1 = t1.intern(VarType::AVar, "AMBIENT TEMPERATURE");
    uint16_t id2 = t2.intern(VarType::AVar, "AMBIENT TEMPERATURE");
    EXPECT_EQ(id1, id2);
}

TEST(InternTableTest, TypicalMsfsVarNamesNoCollision) {
    InternTable table;
    const char* var_names[] = {
        "ELECTRICAL MAIN BUS VOLTAGE",
        "ELECTRICAL BUS GEN1 AMP",
        "ELECTRICAL BATTERY VOLTAGE",
        "ELECTRICAL BATTERY AMP",
        "ELECTRICAL GENALT GEN1 ACTIVE",
        "GENERAL ENG RPM:1",
        "GENERAL ENG RPM:2",
        "ENG FUEL FLOW GPH:1",
        "ENG FUEL FLOW GPH:2",
        "AMBIENT TEMPERATURE",
        "AMBIENT PRESSURE",
        "AMBIENT WIND VELOCITY",
        "AMBIENT WIND DIRECTION",
        "PLANE ALTITUDE",
        "PLANE HEADING DEGREES TRUE",
        "PLANE PITCH DEGREES",
        "PLANE BANK DEGREES",
        "AIRSPEED INDICATED",
        "VERTICAL SPEED",
        "FLAP HANDLE PERCENT",
        "GEAR HANDLE POSITION",
        "BRAKE PARKING POSITION",
        "LIGHT BEACON",
        "LIGHT NAV",
        "LIGHT STROBE",
        "LIGHT LANDING:1",
        "LIGHT TAXI",
        "COM ACTIVE FREQUENCY:1",
        "COM STANDBY FREQUENCY:1",
        "NAV ACTIVE FREQUENCY:1",
        "NAV STANDBY FREQUENCY:1",
        "ADF ACTIVE FREQUENCY:1",
        "TRANSPONDER CODE:1",
        "AUTOPILOT MASTER",
        "AUTOPILOT HEADING LOCK DIR",
        "AUTOPILOT ALTITUDE LOCK VAR",
        "AUTOPILOT VERTICAL HOLD VAR",
        "FUEL TANK LEFT MAIN QUANTITY",
        "FUEL TANK RIGHT MAIN QUANTITY",
        "FUEL TOTAL QUANTITY",
        "HYDRAULIC PRESSURE:1",
        "PITOT HEAT",
        "ANTI ICE STRUCTURAL DEICE",
        "DEICE WINDSHIELD",
        "PRESSURIZATION CABIN ALT",
        "PRESSURIZATION PRESSURE DIFF",
        "SIMULATION RATE",
        "ATC MODEL",
        "TITLE",
        "TOTAL WEIGHT",
        "CG PERCENT",
    };

    int count = sizeof(var_names) / sizeof(var_names[0]);
    std::vector<uint16_t> ids;
    for (int i = 0; i < count; ++i) {
        uint16_t id = table.intern(VarType::AVar, var_names[i]);
        ids.push_back(id);
    }

    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); ++i) {
        EXPECT_NE(ids[i], ids[i-1])
            << "Collision between vars at indices " << (i-1) << " and " << i
            << ": " << var_names[i-1] << " and " << var_names[i];
    }
}

TEST(InternTableTest, InternIdsAreBelowIdSpace) {
    InternTable table;
    uint16_t id = table.intern(VarType::AVar, "SOME_VAR");
    EXPECT_LT(id, ID_SPACE);
}

// =============================================================================
// WireCodec V2 — delta protocol build/parse
// =============================================================================

TEST(WireCodecTest, BuildDeltaReadIs8Bytes) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    size_t written = codec.build_delta_read(buf.data(), buf.size(),
                                              TIER_MASK_FAST, 42);
    EXPECT_EQ(written, 8u);

    auto* hdr = reinterpret_cast<const PacketHeader*>(buf.data());
    EXPECT_EQ(hdr->magic, PACKET_MAGIC);
    EXPECT_EQ(hdr->version, PROTOCOL_VERSION);
    EXPECT_EQ(hdr->cmd, static_cast<uint8_t>(Cmd::DeltaRead));
    EXPECT_EQ(hdr->count, TIER_MASK_FAST);  // count = tier mask
    EXPECT_EQ(hdr->seq_id, 42u);

    // Parse: DeltaRead is header-only — records should be empty
    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.records.size(), 0u);
}

TEST(WireCodecTest, BuildDeltaReadWithAllTiers) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    size_t written = codec.build_delta_read(buf.data(), buf.size(),
                                              TIER_MASK_ALL, 100);
    EXPECT_EQ(written, 8u);

    auto* hdr = reinterpret_cast<const PacketHeader*>(buf.data());
    EXPECT_EQ(hdr->count, TIER_MASK_ALL);
}

TEST(WireCodecTest, BuildDeltaReadForceFullSync) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    size_t written = codec.build_delta_read(buf.data(), buf.size(),
                                              TIER_MASK_FORCE_FULL_SYNC, 1);
    EXPECT_EQ(written, 8u);

    auto* hdr = reinterpret_cast<const PacketHeader*>(buf.data());
    EXPECT_EQ(hdr->count, TIER_MASK_FORCE_FULL_SYNC);
}

TEST(WireCodecTest, BuildDeltaUpdateWith3Records) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    VarRecord records[3];
    records[0].var_type = VarType::AVar;
    records[0].name_id  = 100;
    records[0].value    = WireValue(28.5f);
    records[1].var_type = VarType::LVar;
    records[1].name_id  = 200;
    records[1].value    = WireValue(1.0f);
    records[2].var_type = VarType::EVar;
    records[2].name_id  = 300;
    records[2].value    = WireValue(15.0f);

    size_t written = codec.build_delta_update(buf.data(), buf.size(), records, 7);
    EXPECT_EQ(written, 8u + 3u * 8u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaUpdate));
    EXPECT_EQ(result.header->seq_id, 7u);
    ASSERT_EQ(result.records.size(), 3u);

    EXPECT_FLOAT_EQ(result.records[0].value.f32, 28.5f);
    EXPECT_EQ(result.records[0].var_type, VarType::AVar);
    EXPECT_FLOAT_EQ(result.records[1].value.f32, 1.0f);
    EXPECT_EQ(result.records[1].var_type, VarType::LVar);
    EXPECT_FLOAT_EQ(result.records[2].value.f32, 15.0f);
    EXPECT_EQ(result.records[2].var_type, VarType::EVar);
}

TEST(WireCodecTest, BuildFullSyncWith500Records) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    std::vector<VarRecord> records(500);
    for (size_t i = 0; i < 500; ++i) {
        records[i].var_type = VarType::AVar;
        records[i].name_id  = static_cast<uint16_t>(i);
        records[i].value    = WireValue(static_cast<float>(i));
    }

    size_t written = codec.build_full_sync(buf.data(), buf.size(), records, 42);
    EXPECT_EQ(written, 8u + 500u * 8u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::FullSync));
    EXPECT_EQ(result.header->seq_id, 42u);
    ASSERT_EQ(result.records.size(), 500u);

    // Spot-check first and last
    EXPECT_EQ(result.records[0].name_id, 0u);
    EXPECT_FLOAT_EQ(result.records[0].value.f32, 0.0f);
    EXPECT_EQ(result.records[499].name_id, 499u);
}

TEST(WireCodecTest, BuildDeltaWriteWithChangedOutputs) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    VarRecord records[2];
    records[0].var_type = VarType::AVar;
    records[0].name_id  = 50;
    records[0].value    = WireValue(28.5f);
    records[1].var_type = VarType::HEvent;
    records[1].name_id  = 60;
    records[1].value    = WireValue(1.0f);

    size_t written = codec.build_delta_write(buf.data(), buf.size(), records, 7);
    EXPECT_EQ(written, 8u + 2u * 8u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaWrite));
    EXPECT_EQ(result.header->seq_id, 7u);
    ASSERT_EQ(result.records.size(), 2u);

    EXPECT_FLOAT_EQ(result.records[0].value.f32, 28.5f);
    EXPECT_EQ(result.records[1].var_type, VarType::HEvent);
}

TEST(WireCodecTest, BuildWriteAckIs8Bytes) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    size_t written = codec.build_write_ack(buf.data(), buf.size(), 77);
    EXPECT_EQ(written, sizeof(PacketHeader));

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::WriteAck));
    EXPECT_EQ(result.header->seq_id, 77u);
    EXPECT_EQ(result.header->count, 0u);
    EXPECT_EQ(result.records.size(), 0u);  // No records
}

TEST(WireCodecTest, BuildReturnsZeroOnOverflow) {
    WireCodec codec;
    uint8_t tiny_buf[4];  // Too small even for header

    VarRecord rec;
    EXPECT_EQ(codec.build_delta_update(tiny_buf, 4, {&rec, 1}, 0), 0u);
    EXPECT_EQ(codec.build_full_sync(tiny_buf, 4, {&rec, 1}, 0), 0u);
    EXPECT_EQ(codec.build_delta_write(tiny_buf, 4, {&rec, 1}, 0), 0u);
    EXPECT_EQ(codec.build_delta_read(tiny_buf, 4, TIER_MASK_FAST, 0), 0u);
}

TEST(WireCodecTest, ParseRejectsBadMagic) {
    WireCodec codec;
    uint8_t bad_data[16] = {0xFF, 0xFF, 0x02, 0x01, 0, 0, 0, 0};
    EXPECT_EQ(codec.parse_header(bad_data, 16), nullptr);
}

TEST(WireCodecTest, ParseRejectsBadVersion) {
    WireCodec codec;
    PacketHeader hdr;
    hdr.version = 99;
    uint8_t buf[8];
    std::memcpy(buf, &hdr, 8);
    EXPECT_EQ(codec.parse_header(buf, 8), nullptr);
}

TEST(WireCodecTest, ParseRejectsTruncatedPacket) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    VarRecord rec;
    size_t written = codec.build_delta_update(buf.data(), buf.size(), {&rec, 1}, 0);
    ASSERT_GT(written, 8u);

    // Pass only 10 bytes — not enough for header + 1 record
    EXPECT_EQ(codec.parse_header(buf.data(), 10), nullptr);
}

TEST(WireCodecTest, ParseDeltaUpdateRoundTrip) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    const uint16_t epoch = 12345;
    VarRecord records[3];
    records[0] = {VarType::AVar, ValType::Float32, 100, WireValue(1.5f)};
    records[1] = {VarType::LVar, ValType::Int32, 200, WireValue(int32_t(-42))};
    records[2] = {VarType::BVar, ValType::Bool, 300, WireValue(true)};

    size_t written = codec.build_delta_update(buf.data(), buf.size(), records, epoch);
    ASSERT_GT(written, 0u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaUpdate));
    EXPECT_EQ(result.header->seq_id, epoch);
    ASSERT_EQ(result.records.size(), 3u);

    EXPECT_FLOAT_EQ(result.records[0].value.f32, 1.5f);
    EXPECT_EQ(result.records[1].value.i32, -42);
    EXPECT_EQ(result.records[2].value.b, true);
}

TEST(WireCodecTest, ParseFullSyncRoundTrip) {
    WireCodec codec;
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);

    const uint16_t epoch = 9999;
    VarRecord records[2];
    records[0] = {VarType::AVar, ValType::Float32, 10, WireValue(100.0f)};
    records[1] = {VarType::EVar, ValType::Float32, 20, WireValue(20.0f)};

    size_t written = codec.build_full_sync(buf.data(), buf.size(), records, epoch);
    ASSERT_GT(written, 0u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::FullSync));
    EXPECT_EQ(result.header->seq_id, epoch);
    ASSERT_EQ(result.records.size(), 2u);
    EXPECT_FLOAT_EQ(result.records[0].value.f32, 100.0f);
    EXPECT_FLOAT_EQ(result.records[1].value.f32, 20.0f);
}

TEST(WireCodecTest, ParseEmptyRecordsReturnsEmptySpan) {
    WireCodec codec;

    // DeltaUpdate with 0 records — just header
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);
    size_t written = codec.build_delta_update(buf.data(), buf.size(), {}, 1);
    EXPECT_EQ(written, 8u);

    auto result = codec.parse(buf.data(), written);
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.records.size(), 0u);
}
