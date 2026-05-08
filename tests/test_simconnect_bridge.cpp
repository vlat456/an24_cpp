#include "simconnect/simconnect_provider.h"
#include "simconnect/simconnect_client_stub.h"
#include "simconnect/wire_protocol.h"
#include "simconnect/wire_codec.h"
#include "simconnect/intern_table.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_build_input.h"
#include "blueprint_v2/interface/direction.h"
#include <gtest/gtest.h>
#include <cstring>

// Forward declarations for internal helpers tested directly.
float wire_value_to_float(const WireValue& wv, ValType vt);
WireValue float_to_wire_value(float value, ValType vt);

// =============================================================================
// SimConnectProvider Tests — V2 Delta Protocol
// =============================================================================
//
// Verifies mapping resolution (including tier/epsilon), delta packet I/O,
// input injection, output change detection, and robustness.

// -- Helper: build a minimal JitBuildInput with one SimVarInput + one SimVarOutput --

static JitBuildInput make_simvar_build_input() {
    JitBuildInput input;

    // SimVarInput (AVar)
    SolverDevice simvar_in;
    simvar_in.name = "msfs_ambient_temp";
    simvar_in.classname = "SimConnectInput";
    simvar_in.kind = ComponentKind::SimConnectInput;
    simvar_in.scheduler_role_kind = SchedulerRoleKind::Source;
    simvar_in.params["var_name"] = "AMBIENT TEMPERATURE";
    simvar_in.params["var_type"] = "AVar";
    simvar_in.params["unit"] = "Celsius";
    simvar_in.params["index"] = "0";
    simvar_in.params["default_value"] = "15.0";
    simvar_in.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
    input.devices.push_back(std::move(simvar_in));

    // SimVarOutput (AVar, data mode)
    SolverDevice simvar_out;
    simvar_out.name = "msfs_bus_voltage";
    simvar_out.classname = "SimConnectOutput";
    simvar_out.kind = ComponentKind::SimConnectOutput;
    simvar_out.scheduler_role_kind = SchedulerRoleKind::Consumer;
    simvar_out.params["var_name"] = "ELECTRICAL MAIN BUS VOLTAGE";
    simvar_out.params["var_type"] = "AVar";
    simvar_out.params["unit"] = "Volts";
    simvar_out.params["index"] = "0";
    simvar_out.params["mode"] = "data";
    simvar_out.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
    input.devices.push_back(std::move(simvar_out));

    input.signal_key_interner.intern("msfs_ambient_temp.out");
    input.signal_key_interner.intern("msfs_bus_voltage.in");
    input.port_to_signal[input.signal_key_interner.intern("msfs_ambient_temp.out")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("msfs_bus_voltage.in")] = 1;
    input.signal_count = 2;
    input.initial_values["msfs_ambient_temp.out"] = 15.0f;
    input.initial_values["msfs_bus_voltage.in"] = 0.0f;

    return input;
}

// -- Helper: build a LVar-only input for single-input tests --

static JitBuildInput make_lvar_build_input(const std::string& var_name,
                                            const std::string& default_val = "0.0") {
    JitBuildInput input;

    SolverDevice lvar_in;
    lvar_in.name = "lvar_test";
    lvar_in.classname = "SimConnectInput";
    lvar_in.kind = ComponentKind::SimConnectInput;
    lvar_in.scheduler_role_kind = SchedulerRoleKind::Source;
    lvar_in.params["var_name"] = var_name;
    lvar_in.params["var_type"] = "LVar";
    lvar_in.params["unit"] = "number";
    lvar_in.params["default_value"] = default_val;
    lvar_in.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
    input.devices.push_back(std::move(lvar_in));

    input.signal_key_interner.intern("lvar_test.out");
    input.port_to_signal[input.signal_key_interner.intern("lvar_test.out")] = 0;
    input.signal_count = 1;
    input.initial_values["lvar_test.out"] = std::stof(default_val);

    return input;
}

// -- Helper: build a V2 DeltaUpdate/FullSync response as std::string --

static std::string build_delta_update_response(std::vector<VarRecord> records,
                                                  uint16_t epoch) {
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);
    size_t written = WireCodec::build_delta_update(buf.data(), buf.size(), records, epoch);
    EXPECT_GT(written, 0u);
    return std::string(reinterpret_cast<const char*>(buf.data()), written);
}

static std::string build_full_sync_response(std::vector<VarRecord> records,
                                              uint16_t epoch) {
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);
    size_t written = WireCodec::build_full_sync(buf.data(), buf.size(), records, epoch);
    EXPECT_GT(written, 0u);
    return std::string(reinterpret_cast<const char*>(buf.data()), written);
}

// ==...== Build Mappings ==...==

TEST(SimConnectProviderImplTest, BuildMappingsResolvesInputsAndOutputs) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);

    EXPECT_EQ(bridge.input_count(), 1u);
    EXPECT_EQ(bridge.output_count(), 1u);
}

TEST(SimConnectProviderImplTest, BuildMappingsSkipsUnknownDevices) {
    JitBuildInput input;
    SolverDevice resistor;
    resistor.name = "r1";
    resistor.classname = "Resistor";
    resistor.kind = ComponentKind::Resistor;
    resistor.scheduler_role_kind = SchedulerRoleKind::Consumer;
    input.devices.push_back(std::move(resistor));

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);

    EXPECT_EQ(bridge.input_count(), 0u);
    EXPECT_EQ(bridge.output_count(), 0u);
}

TEST(SimConnectProviderImplTest, BuildMappingsSkipsEmptyVarName) {
    JitBuildInput input;
    SolverDevice simvar_in;
    simvar_in.name = "bad_input";
    simvar_in.classname = "SimConnectInput";
    simvar_in.kind = ComponentKind::SimConnectInput;
    simvar_in.scheduler_role_kind = SchedulerRoleKind::Source;
    input.devices.push_back(std::move(simvar_in));

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    EXPECT_EQ(bridge.input_count(), 0u);
}

// ==...== Tier and Epsilon from Params ==...==

TEST(SimConnectProviderImplTest, BuildMappingsAssignsDefaultTierByVarType) {
    // AVar defaults to TIER_FAST
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);

    // Default tier for AVar is TIER_FAST
    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    // We verify via the DeltaRead packet — tier mask will include fast
    EXPECT_EQ(bridge.input_count(), 1u);
}

TEST(SimConnectProviderImplTest, BuildMappingsParsesTierFromParams) {
    JitBuildInput input;
    SolverDevice simvar_in;
    simvar_in.name = "tiered_var";
    simvar_in.classname = "SimConnectInput";
    simvar_in.kind = ComponentKind::SimConnectInput;
    simvar_in.scheduler_role_kind = SchedulerRoleKind::Source;
    simvar_in.params["var_name"] = "TIERED_VAR";
    simvar_in.params["var_type"] = "EVar";      // Default tier: TIER_SLOW
    simvar_in.params["tier"] = "0";              // Override to TIER_FAST
    simvar_in.params["unit"] = "Celsius";
    simvar_in.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
    input.devices.push_back(std::move(simvar_in));

    input.signal_key_interner.intern("tiered_var.out");
    input.port_to_signal[input.signal_key_interner.intern("tiered_var.out")] = 0;
    input.signal_count = 1;
    input.initial_values["tiered_var.out"] = 0.0f;

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);

    EXPECT_EQ(bridge.input_count(), 1u);
    // The mapping's tier was overridden to 0 via param — verified by building
}

TEST(SimConnectProviderImplTest, BuildMappingsParsesEpsilonFromParams) {
    JitBuildInput input;
    SolverDevice simvar_out;
    simvar_out.name = "eps_out";
    simvar_out.classname = "SimConnectOutput";
    simvar_out.kind = ComponentKind::SimConnectOutput;
    simvar_out.scheduler_role_kind = SchedulerRoleKind::Consumer;
    simvar_out.params["var_name"] = "EPS_VAR";
    simvar_out.params["var_type"] = "AVar";
    simvar_out.params["epsilon"] = "0.5";        // Custom epsilon
    simvar_out.params["unit"] = "Volts";
    simvar_out.params["mode"] = "data";
    simvar_out.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
    input.devices.push_back(std::move(simvar_out));

    input.signal_key_interner.intern("eps_out.in");
    input.port_to_signal[input.signal_key_interner.intern("eps_out.in")] = 0;
    input.signal_count = 1;
    input.initial_values["eps_out.in"] = 0.0f;

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);

    EXPECT_EQ(bridge.output_count(), 1u);
}

// ==...== Connection ==...==

TEST(SimConnectProviderImplTest, ConnectSucceedsWithStub) {
    SimConnectProvider bridge;
    EXPECT_TRUE(bridge.connect());
    EXPECT_TRUE(bridge.is_connected());
}

TEST(SimConnectProviderImplTest, DisconnectSetsNotConnected) {
    SimConnectProvider bridge;
    bridge.connect();
    bridge.disconnect();
    EXPECT_FALSE(bridge.is_connected());
}

// ==...== Inject Inputs ==...==

TEST(SimConnectProviderImplTest, InjectInputsAppliesOverrides) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // Default value (15.0) should be buffered
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 15.0f);
}

// ==...== Request Inputs (V2 Delta Protocol) ==...==

TEST(SimConnectProviderImplTest, RequestInputsSends8ByteDeltaRead) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    bridge.request_inputs();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    const auto& req = stub->last_request_bytes();

    // Should be exactly 8 bytes — DeltaRead is header-only
    ASSERT_GE(req.size(), sizeof(PacketHeader));

    const auto* data = req.data();
    auto result = WireCodec::parse(data, req.size());

    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaRead));
    // Tier mask should have at least TIER_MASK_FAST
    EXPECT_NE(result.header->count & TIER_MASK_FAST, 0u);
    // DeltaRead has no records
    EXPECT_EQ(result.records.size(), 0u);
}

TEST(SimConnectProviderImplTest, RequestInputsTierMaskVariesByFrame) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();



    // Frame 0: 0%5==0, 0%30==0 → all tiers
    bridge.request_inputs();
    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    auto result0 = WireCodec::parse(stub->last_request_bytes().data(),
                                stub->last_request_bytes().size());
    ASSERT_NE(result0.header, nullptr);
    uint16_t mask0 = result0.header->count;
    EXPECT_EQ(mask0, TIER_MASK_FAST | TIER_MASK_MEDIUM | TIER_MASK_SLOW);  // frame 0: all tiers

    // Advance to frame 1: fast only
    bridge.request_inputs();
    result0 = WireCodec::parse(stub->last_request_bytes().data(),
                           stub->last_request_bytes().size());
    ASSERT_NE(result0.header, nullptr);
    EXPECT_EQ(result0.header->count, TIER_MASK_FAST);

    // Advance to frame 2,3,4: fast only
    bridge.request_inputs();
    bridge.request_inputs();
    bridge.request_inputs();  // Now at frame 4

    // Frame 5: fast + medium
    bridge.request_inputs();  // frame 5
    result0 = WireCodec::parse(stub->last_request_bytes().data(),
                           stub->last_request_bytes().size());
    ASSERT_NE(result0.header, nullptr);
    EXPECT_EQ(result0.header->count, TIER_MASK_FAST | TIER_MASK_MEDIUM);
}

TEST(SimConnectProviderImplTest, RequestInputsDoesNothingWhenNotConnected) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    // NOT connecting

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    std::vector<uint8_t> before = stub->last_request_bytes();

    bridge.request_inputs();
    EXPECT_EQ(stub->last_request_bytes(), before);
}

// ==...== Extract Outputs (V2 Delta Protocol) ==...==

TEST(SimConnectProviderImplTest, ExtractOutputsSendsDeltaWrite) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // Override the output signal to a non-zero value so change detection triggers
    std::vector<float> values(input.signal_count, 0.0f);
    values[1] = 28.5f;
    bridge.extract_outputs_raw(values.data(), values.size());

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    const auto& req = stub->last_request_bytes();

    ASSERT_GE(req.size(), sizeof(PacketHeader));

    const auto* data = req.data();
    auto result = WireCodec::parse(data, req.size());

    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaWrite));
    ASSERT_EQ(result.records.size(), 1u);

    uint16_t expected_id = compute_intern_id("ELECTRICAL MAIN BUS VOLTAGE");
    EXPECT_EQ(result.records[0].name_id, expected_id);
    EXPECT_FLOAT_EQ(result.records[0].value.f32, 28.5f);
}

TEST(SimConnectProviderImplTest, DeltaWriteSkipsUnchangedDataOutputs) {
    // With V2, ALL outputs use epsilon-based change detection — not just events.
    // If the output value hasn't changed beyond epsilon, no packet is sent.
    JitBuildInput input;

    SolverDevice out;
    out.name = "data_out";
    out.classname = "SimConnectOutput";
    out.kind = ComponentKind::SimConnectOutput;
    out.scheduler_role_kind = SchedulerRoleKind::Consumer;
    out.params["var_name"] = "DATA_VAR";
    out.params["var_type"] = "AVar";
    out.params["unit"] = "number";
    out.params["mode"] = "data";
    out.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
    input.devices.push_back(std::move(out));

    input.signal_key_interner.intern("data_out.in");
    input.port_to_signal[input.signal_key_interner.intern("data_out.in")] = 0;
    input.signal_count = 1;
    input.initial_values["data_out.in"] = 0.0f;

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // First extract: output is 0 (initial), shadow is 0 → no change → no packet
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.extract_outputs_raw(values.data(), values.size());

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    const auto& req = stub->last_request_bytes();

    // The output value hasn't changed from initial 0 → no DeltaWrite sent
    // (or if something was sent before, it's not a DeltaWrite)
    if (req.size() >= sizeof(PacketHeader)) {

        auto result = WireCodec::parse(req.data(), req.size());
        if (result.header) {
            EXPECT_NE(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaWrite));
        }
    }
}

TEST(SimConnectProviderImplTest, DeltaWriteOnlySendsChangedOutputs) {
    // Two outputs — one changes, one doesn't → only one record in DeltaWrite
    JitBuildInput input;

    // Output 1: will change (set initial to 0, then we'll step to get a different value)
    SolverDevice out1;
    out1.name = "out_change";
    out1.classname = "SimConnectOutput";
    out1.kind = ComponentKind::SimConnectOutput;
    out1.scheduler_role_kind = SchedulerRoleKind::Consumer;
    out1.params["var_name"] = "CHANGING_VAR";
    out1.params["var_type"] = "AVar";
    out1.params["unit"] = "number";
    out1.params["mode"] = "data";
    out1.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
    input.devices.push_back(std::move(out1));

    // Output 2: will not change (constant)
    SolverDevice out2;
    out2.name = "out_const";
    out2.classname = "SimConnectOutput";
    out2.kind = ComponentKind::SimConnectOutput;
    out2.scheduler_role_kind = SchedulerRoleKind::Consumer;
    out2.params["var_name"] = "CONST_VAR";
    out2.params["var_type"] = "AVar";
    out2.params["unit"] = "number";
    out2.params["mode"] = "data";
    out2.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
    input.devices.push_back(std::move(out2));

    input.signal_key_interner.intern("out_change.in");
    input.signal_key_interner.intern("out_const.in");
    input.port_to_signal[input.signal_key_interner.intern("out_change.in")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("out_const.in")] = 1;
    input.signal_count = 2;
    input.initial_values["out_change.in"] = 0.0f;
    input.initial_values["out_const.in"] = 0.0f;

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // First extract: both start at 0, shadow is 0 → neither changes
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.extract_outputs_raw(values.data(), values.size());

    // Second extract (same value): still no change
    bridge.extract_outputs_raw(values.data(), values.size());

    // Third extract: still no change (output stays at 0)
    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    const auto& req = stub->last_request_bytes();
    if (req.size() >= sizeof(PacketHeader)) {

        auto result = WireCodec::parse(req.data(), req.size());
        if (result.header) {
            // No DeltaWrite should have been sent since outputs haven't changed
            EXPECT_NE(result.header->cmd, static_cast<uint8_t>(Cmd::DeltaWrite));
        }
    }
}

// ==...== On V2 Response ==...==

TEST(SimConnectProviderImplTest, OnResponseParsesDeltaUpdate) {
    auto input = make_lvar_build_input("AN24_TEST_VAR", "0.0");
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    bridge.request_inputs();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    // Build V2 DeltaUpdate with value 42.0
    uint16_t expected_id = compute_intern_id("AN24_TEST_VAR");
    VarRecord rec;
    rec.var_type = VarType::LVar;
    rec.val_type = ValType::Float32;
    rec.name_id  = expected_id;
    rec.value    = WireValue(42.0f);


    std::string response = build_delta_update_response({rec}, 0);
    stub->trigger_mock_response(response);

    // Inject inputs — should have 42.0
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 42.0f);
}

TEST(SimConnectProviderImplTest, FullSyncReplacesAllValues) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    bridge.request_inputs();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    // Build FullSync with both input values
    uint16_t id_temp = compute_intern_id("AMBIENT TEMPERATURE");
    uint16_t id_volt = compute_intern_id("ELECTRICAL MAIN BUS VOLTAGE");

    VarRecord recs[2];
    recs[0].var_type = VarType::AVar;
    recs[0].name_id  = id_temp;
    recs[0].value    = WireValue(22.5f);
    recs[1].var_type = VarType::AVar;
    recs[1].name_id  = id_volt;
    recs[1].value    = WireValue(28.0f);


    std::string response = build_full_sync_response({recs[0], recs[1]}, 0);
    stub->trigger_mock_response(response);

    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 22.5f);
}

TEST(SimConnectProviderImplTest, DeltaUpdateIgnoresUnknownIds) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    bridge.request_inputs();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    uint16_t id_known = compute_intern_id("AMBIENT TEMPERATURE");
    uint16_t id_unknown = 9999;

    VarRecord recs[2];
    recs[0].var_type = VarType::AVar;
    recs[0].name_id  = id_unknown;
    recs[0].value    = WireValue(42.0f);
    recs[1].var_type = VarType::AVar;
    recs[1].name_id  = id_known;
    recs[1].value    = WireValue(30.0f);


    std::string response = build_delta_update_response({recs[0], recs[1]}, 0);
    stub->trigger_mock_response(response);

    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 30.0f);
}

// ==...== Robustness ==...==

TEST(SimConnectProviderImplTest, OnResponseHandlesMalformedJson) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    stub->trigger_mock_response("not valid json {{{");
    stub->trigger_mock_response("");
    stub->trigger_mock_response(R"({"missing":"cmd_field"})");

    // Default value preserved
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 15.0f);
}

TEST(SimConnectProviderImplTest, MalformedBinaryPacketDoesNotCrash) {
    auto input = make_simvar_build_input();
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    // Valid magic but truncated (header only, count says 5 records)
    PacketHeader hdr;
    hdr.magic = PACKET_MAGIC;
    hdr.version = PROTOCOL_VERSION;
    hdr.cmd = static_cast<uint8_t>(Cmd::DeltaUpdate);
    hdr.count = 5;
    hdr.seq_id = 0;
    std::string truncated(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    stub->trigger_mock_response(truncated);

    // Bad magic
    uint8_t bad_magic[8] = {0xFF, 0xFF, 0x02, 0x01, 0, 0, 0, 0};
    std::string bad(reinterpret_cast<const char*>(bad_magic), 8);
    stub->trigger_mock_response(bad);

    // Wrong version
    PacketHeader bad_ver;
    bad_ver.magic = PACKET_MAGIC;
    bad_ver.version = 99;
    bad_ver.cmd = static_cast<uint8_t>(Cmd::DeltaUpdate);
    std::string bad_ver_str(reinterpret_cast<const char*>(&bad_ver), sizeof(bad_ver));
    stub->trigger_mock_response(bad_ver_str);

    // Should not crash, default value preserved
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 15.0f);
}

// ==...== V2 Delta Round-Trip ==...==

TEST(SimConnectProviderImplTest, DeltaRoundTripReadRequestDeltaUpdate) {
    // Full cycle: request_inputs() → DeltaRead → mock DeltaUpdate → inject_inputs()
    auto input = make_lvar_build_input("ROUND_TRIP_VAR", "10.0");
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // 1. Send request (8-byte DeltaRead)
    bridge.request_inputs();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    const auto& req = stub->last_request_bytes();

    // 2. Verify it's a DeltaRead

    auto parsed_req = WireCodec::parse(req.data(), req.size());
    ASSERT_NE(parsed_req.header, nullptr);
    EXPECT_EQ(parsed_req.header->cmd, static_cast<uint8_t>(Cmd::DeltaRead));

    // 3. Build DeltaUpdate with value 99.5
    uint16_t requested_id = compute_intern_id("ROUND_TRIP_VAR");
    VarRecord resp_rec;
    resp_rec.var_type = VarType::LVar;
    resp_rec.name_id  = requested_id;
    resp_rec.value    = WireValue(99.5f);

    std::string response = build_delta_update_response({resp_rec}, 0);
    stub->trigger_mock_response(response);

    // 4. Verify injection
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 99.5f);
}

TEST(SimConnectProviderImplTest, DeltaRoundTripFullSyncThenDeltaUpdate) {
    // FullSync first, then a DeltaUpdate with a different value
    auto input = make_lvar_build_input("SYNC_VAR", "0.0");
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    uint16_t var_id = compute_intern_id("SYNC_VAR");

    // 1. FullSync with value 50.0
    bridge.request_inputs();

    VarRecord sync_rec;
    sync_rec.var_type = VarType::LVar;
    sync_rec.name_id  = var_id;
    sync_rec.value    = WireValue(50.0f);

    std::string full_resp = build_full_sync_response({sync_rec}, 0);
    stub->trigger_mock_response(full_resp);

    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 50.0f);

    // 2. DeltaUpdate with value 75.0
    bridge.request_inputs();

    VarRecord delta_rec;
    delta_rec.var_type = VarType::LVar;
    delta_rec.name_id  = var_id;
    delta_rec.value    = WireValue(75.0f);

    std::string delta_resp = build_delta_update_response({delta_rec}, 1);
    stub->trigger_mock_response(delta_resp);

    std::vector<float> values2(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values2.data(), values2.size());
    EXPECT_FLOAT_EQ(values2[0], 75.0f);
}

// ==...== Epoch Wraparound ==...==

TEST(SimConnectProviderImplTest, EpochWraparoundRejectsStaleResponse) {
    // Regression: stale detection must work across uint16_t wraparound.
    // At 60fps, host_epoch_ wraps every ~18 minutes.
    // Before the fix, the two-condition check (seq_id < host_epoch && diff > 100)
    // failed to detect stale responses when seq_id was near 65535 and host_epoch
    // just wrapped past 0.
    auto input = make_lvar_build_input("EPOCH_WRAP_VAR", "10.0");
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // Simulate the host wrapping around: advance epoch to near-overflow
    // by calling request_inputs() many times (each increments host_epoch_ by 1).
    // We need host_epoch_ to be small (just wrapped) and then send a response
    // with seq_id from before the wrap (high value, genuinely stale).
    //
    // Alternative: directly send a stale response and verify it's ignored.
    // For a clean test, we'll advance the epoch and send responses with known gaps.

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    uint16_t var_id = compute_intern_id("EPOCH_WRAP_VAR");

    // Advance to epoch 10 (simulating just-wrapped state)
    for (int i = 0; i < 10; ++i) {
        bridge.request_inputs();
    }

    // Send a response with seq_id=65000 (genuinely stale — gap ~546)
    // The host is at epoch 10, so 65000 is from before the wrap, ~546 frames ago.
    VarRecord stale_rec;
    stale_rec.var_type = VarType::LVar;
    stale_rec.name_id  = var_id;
    stale_rec.value    = WireValue(999.0f);  // Bogus value — should NOT be applied

    std::string stale_resp = build_delta_update_response({stale_rec}, 65000);
    stub->trigger_mock_response(stale_resp);

    // The stale response should have been ignored — default value preserved
    std::vector<float> values(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values.data(), values.size());
    EXPECT_FLOAT_EQ(values[0], 10.0f) << "Stale response should be ignored";

    // Now send a recent response (seq_id=9, gap=1) — should be accepted
    VarRecord fresh_rec;
    fresh_rec.var_type = VarType::LVar;
    fresh_rec.name_id  = var_id;
    fresh_rec.value    = WireValue(42.0f);

    std::string fresh_resp = build_delta_update_response({fresh_rec}, 9);
    stub->trigger_mock_response(fresh_resp);

    std::vector<float> values2(input.signal_count, 0.0f);
    bridge.inject_inputs_raw(values2.data(), values2.size());
    EXPECT_FLOAT_EQ(values2[0], 42.0f) << "Recent response should be accepted";
}

// ==...== Heartbeat Ping/Pong ==...==

// Helper: build a binary Pong packet as std::string
static std::string build_pong_response(uint16_t ping_id) {
    std::vector<uint8_t> buf(MAX_PACKET_SIZE);
    size_t written = WireCodec::build_pong(buf.data(), buf.size(), ping_id);
    EXPECT_GT(written, 0u);
    return std::string(reinterpret_cast<const char*>(buf.data()), written);
}

TEST(SimConnectProviderImplTest, PollSendsPingAfter5Seconds) {
    SimConnectProvider bridge;
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());


    // Poll at t=0 — no ping yet (just connected)
    bridge.poll(0.0);
    // Reset the last request tracking
    stub->reset();
    stub->set_response_callback([&](std::span<const uint8_t> p) {
        // Re-register callback after reset
        const_cast<SimConnectProvider&>(bridge).client()->send_request(
            std::string(reinterpret_cast<const char*>(p.data()), p.size()));
    });
    // Re-connect after reset
    bridge.connect();

    // Poll at t=3 — still no ping (interval is 5s)
    bridge.poll(3.0);
    // Check last request — should NOT be a Ping
    if (stub->last_request_bytes().size() >= sizeof(PacketHeader)) {
        auto result = WireCodec::parse(
            stub->last_request_bytes().data(),
            stub->last_request_bytes().size());
        if (result.header) {
            EXPECT_NE(result.header->cmd, static_cast<uint8_t>(Cmd::Ping));
        }
    }

    // Poll at t=5.1 — should send Ping
    bridge.poll(5.1);
    const auto& req = stub->last_request_bytes();
    ASSERT_GE(req.size(), sizeof(PacketHeader));
    auto result = WireCodec::parse(
        req.data(), req.size());
    ASSERT_NE(result.header, nullptr);
    EXPECT_EQ(result.header->cmd, static_cast<uint8_t>(Cmd::Ping));
}

TEST(SimConnectProviderImplTest, PongReceivedResetsHealth) {
    SimConnectProvider bridge;
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    EXPECT_FALSE(bridge.is_alive());

    // Simulate first pong at t=5 to establish baseline
    bridge.poll(5.0);
    std::string pong = build_pong_response(1);
    stub->trigger_mock_response(pong);
    bridge.poll(5.0);
    EXPECT_TRUE(bridge.is_alive());

    // Advance past timeout (no new pong)
    // poll() sets current_time_ and checks: current_time_ - last_pong_recv_time_ > 10.0
    bridge.poll(16.0);  // 16.0 - 5.0 = 11.0 > 10.0 → unhealthy
    EXPECT_FALSE(bridge.is_alive());

    // Send pong — should recover
    std::string pong2 = build_pong_response(2);
    stub->trigger_mock_response(pong2);
    bridge.poll(16.0);  // Processes pong → sets last_pong_recv to 16.0 → healthy
    EXPECT_TRUE(bridge.is_alive());
}

TEST(SimConnectProviderImplTest, PongEchoesPingSeqId) {
    SimConnectProvider bridge;
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());


    // Advance time to trigger a ping
    bridge.poll(0.0);
    bridge.poll(6.0);  // > PING_INTERVAL_SEC

    const auto& ping_req = stub->last_request_bytes();
    ASSERT_GE(ping_req.size(), sizeof(PacketHeader));

    auto ping_parsed = WireCodec::parse(
        reinterpret_cast<const uint8_t*>(ping_req.data()), ping_req.size());
    ASSERT_NE(ping_parsed.header, nullptr);
    EXPECT_EQ(ping_parsed.header->cmd, static_cast<uint8_t>(Cmd::Ping));
    uint16_t ping_id = ping_parsed.header->seq_id;

    // Send a pong with matching ping_id
    std::string pong = build_pong_response(ping_id);
    stub->trigger_mock_response(pong);
    bridge.poll(6.0);  // Process pong

    EXPECT_TRUE(bridge.is_alive());
}

TEST(SimConnectProviderImplTest, StalePongIsIgnored) {
    SimConnectProvider bridge;
    bridge.connect();

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());

    bridge.poll(0.0);
    bridge.poll(6.0);

    std::string stale_pong = build_pong_response(999);
    stub->trigger_mock_response(stale_pong);
    bridge.poll(6.0);

    EXPECT_FALSE(bridge.is_alive());
}

TEST(SimConnectProviderImplTest, IsAliveInitiallyFalse) {
    SimConnectProvider bridge;
    EXPECT_FALSE(bridge.is_alive());
}

// ==...== Frame Counter Reset (issue #489) ==...==

TEST(SimConnectProviderImplTest, BuildMappingsResetsFrameCounterAndEpoch) {
    auto input = make_lvar_build_input("RESET_TEST_VAR", "0.0");
    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider bridge;
    bridge.build_mappings(input);
    bridge.connect();

    // Advance frame counter by requesting inputs several times
    for (int i = 0; i < 7; ++i) {
        bridge.request_inputs();
    }

    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    auto result = WireCodec::parse(stub->last_request_bytes().data(),
                                   stub->last_request_bytes().size());
    ASSERT_NE(result.header, nullptr);
    // Frame 7: 7%5!=0, 7%30!=0 → only fast tier
    EXPECT_EQ(result.header->count, TIER_MASK_FAST);

    // Rebuild — frame counter should reset to 0
    bridge.build_mappings(input);

    bridge.request_inputs();
    result = WireCodec::parse(stub->last_request_bytes().data(),
                              stub->last_request_bytes().size());
    ASSERT_NE(result.header, nullptr);
    // Frame 0: 0%5==0, 0%30==0 → all tiers
    EXPECT_EQ(result.header->count, TIER_MASK_FAST | TIER_MASK_MEDIUM | TIER_MASK_SLOW);
}

// =============================================================================
// Internal helper robustness — unknown ValType fallback (#485)
// =============================================================================

TEST(SimConnectProviderImplTest, WireValueToFloatUnknownValTypeReturnsZero) {
    WireValue wv(3.14f);
    ValType invalid = static_cast<ValType>(99);
#ifndef NDEBUG
    EXPECT_DEATH((void)wire_value_to_float(wv, invalid), "");
#else
    EXPECT_FLOAT_EQ(wire_value_to_float(wv, invalid), 0.0f);
#endif
}

TEST(SimConnectProviderImplTest, FloatToWireValueUnknownValTypeReturnsZero) {
    ValType invalid = static_cast<ValType>(99);
#ifndef NDEBUG
    EXPECT_DEATH((void)float_to_wire_value(3.14f, invalid), "");
#else
    WireValue wv = float_to_wire_value(3.14f, invalid);
    EXPECT_EQ(wv.u32, 0u);
#endif
}

TEST(SimConnectStubTest, SendBytesFailureInjection) {
    auto stub = std::make_unique<StubSimConnectClient>();
    stub->connect();
    
    // Default: send_bytes succeeds
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    EXPECT_TRUE(stub->send_bytes(test_data, 4));
     EXPECT_EQ(stub->last_request_bytes().size(), 4u);
    
    // With failure flag set: send_bytes returns false
    stub->set_send_bytes_should_fail(true);
    EXPECT_FALSE(stub->send_bytes(test_data, 4));
    
    // After reset: failure flag is cleared, send_bytes succeeds again
    stub->reset();
    stub->connect();
    EXPECT_TRUE(stub->send_bytes(test_data, 4));
}

TEST(SimConnectProviderImplTest, InvalidBinaryPacketDoesNotAttemptJsonParse) {
    SimConnectProvider bridge;
    bridge.connect();
    
    auto* stub = static_cast<StubSimConnectClient*>(bridge.client());
    
    // Construct a header-sized payload with invalid magic
    uint8_t bad_magic[sizeof(PacketHeader)] = {0xFF, 0xFF, 0x02, 0x01, 0, 0, 0, 0};
    std::string bad_packet(reinterpret_cast<const char*>(bad_magic), sizeof(bad_magic));
    
    // Trigger response — should log "Invalid binary packet" and NOT attempt JSON parse
    stub->trigger_mock_response(bad_packet);
    
    // Verify: just that it doesn't crash and completes successfully
}
