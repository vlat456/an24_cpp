#include "simconnect/simconnect_coordinator.h"
#include "simconnect/simconnect_provider.h"
#include "simconnect/simconnect_client_stub.h"
#include "simconnect/wire_protocol.h"
#include "core/solvers/jit/jit_build_input.h"
#include "core/solvers/jit/components/all.h"
#include "blueprint_v2/interface/direction.h"
#include <gtest/gtest.h>

// =============================================================================
// Helpers
// =============================================================================

static JitBuildInput make_coord_test_input() {
    JitBuildInput input;
    SolverDevice dev;
    dev.name = "test_sensor";
    dev.classname = "SimConnectInput";
    dev.kind = ComponentKind::SimConnectInput;
    dev.scheduler_role_kind = SchedulerRoleKind::Source;
    dev.params["var_name"] = "AMBIENT TEMPERATURE";
    dev.params["var_type"] = "AVar";
    dev.params["unit"] = "Celsius";
    dev.params["index"] = "0";
    dev.params["default_value"] = "15.0";
    dev.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
    input.devices.push_back(std::move(dev));
    input.signal_key_interner.intern("test_sensor.out");
    input.port_to_signal[input.signal_key_interner.intern("test_sensor.out")] = 0;
    input.signal_count = 1;
    return input;
}

// =============================================================================
// SimConnectCoordinator Tests
// =============================================================================

class CoordinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        SimConnectCoordinator::reset_instance();
    }
    void TearDown() override {
        SimConnectCoordinator::reset_instance();
    }
};

TEST_F(CoordinatorTest, SingletonReturnsSameInstance) {
    auto& a = SimConnectCoordinator::instance();
    auto& b = SimConnectCoordinator::instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(CoordinatorTest, ResetInstanceResetsState) {
    auto& a = SimConnectCoordinator::instance();
    a.connect(nullptr);
    EXPECT_TRUE(a.is_connected());
    SimConnectCoordinator::reset_instance();
    auto& b = SimConnectCoordinator::instance();
    EXPECT_FALSE(b.is_connected());
}

TEST_F(CoordinatorTest, ConnectOpensClientOnFirstCall) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    auto* stub_ptr = stub.get();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider(coord);
    coord.add_provider(&provider);

    EXPECT_FALSE(coord.is_connected());
    EXPECT_TRUE(coord.connect(&provider));
    EXPECT_TRUE(coord.is_connected());
    EXPECT_TRUE(stub_ptr->is_connected());
}

TEST_F(CoordinatorTest, ConnectReusesClientForSecondProvider) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    auto* stub_ptr = stub.get();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider_a(coord);
    SimConnectProvider provider_b(coord);
    coord.add_provider(&provider_a);
    coord.add_provider(&provider_b);

    EXPECT_TRUE(coord.connect(&provider_a));
    EXPECT_TRUE(coord.connect(&provider_b));
    EXPECT_EQ(coord.connect_count(), 2u);
    EXPECT_TRUE(stub_ptr->is_connected());
}

TEST_F(CoordinatorTest, DisconnectClosesClientWhenLastProvider) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    auto* stub_ptr = stub.get();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider(coord);
    coord.add_provider(&provider);

    coord.connect(&provider);
    EXPECT_TRUE(coord.is_connected());

    coord.disconnect(&provider);
    EXPECT_FALSE(coord.is_connected());
    EXPECT_FALSE(stub_ptr->is_connected());
}

TEST_F(CoordinatorTest, DisconnectDoesNotCloseWhenOtherProvidersActive) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    auto* stub_ptr = stub.get();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider_a(coord);
    SimConnectProvider provider_b(coord);
    coord.add_provider(&provider_a);
    coord.add_provider(&provider_b);

    coord.connect(&provider_a);
    coord.connect(&provider_b);

    coord.disconnect(&provider_a);
    EXPECT_TRUE(coord.is_connected());
    EXPECT_TRUE(stub_ptr->is_connected());
    EXPECT_EQ(coord.connect_count(), 1u);

    coord.disconnect(&provider_b);
    EXPECT_FALSE(coord.is_connected());
}

TEST_F(CoordinatorTest, ConnectIsIdempotentForSameProvider) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider(coord);
    coord.add_provider(&provider);

    EXPECT_TRUE(coord.connect(&provider));
    EXPECT_TRUE(coord.connect(&provider));  // Second call should be no-op
    EXPECT_EQ(coord.connect_count(), 1u);
}

TEST_F(CoordinatorTest, BroadcastsResponseToAllProviders) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider_a(coord);
    SimConnectProvider provider_b(coord);
    coord.add_provider(&provider_a);
    coord.add_provider(&provider_b);

    // Build minimal mappings so both providers can receive responses
    auto input = make_coord_test_input();
    JIT_Simulator sim;
    sim.start(input);
    provider_a.build_mappings(input);
    provider_b.build_mappings(input);

    coord.connect(&provider_a);
    coord.connect(&provider_b);

    // Trigger a ping from each provider (required for pong to be accepted)
    provider_a.poll(5.1);
    provider_b.poll(5.1);

    // Build a Pong response matching the ping id=1
    PacketHeader pong_hdr;
    pong_hdr.magic = PACKET_MAGIC;
    pong_hdr.version = PROTOCOL_VERSION;
    pong_hdr.cmd = static_cast<uint8_t>(Cmd::Pong);
    pong_hdr.count = 0;
    pong_hdr.seq_id = 1;
    std::vector<uint8_t> pong_data(sizeof(PacketHeader));
    std::memcpy(pong_data.data(), &pong_hdr, sizeof(pong_hdr));

    // Trigger response — both providers should process it
    auto* client = static_cast<StubSimConnectClient*>(*coord.client());
    client->trigger_mock_response(pong_data);

    // Both providers should have received the pong and reset health
    EXPECT_TRUE(provider_a.is_alive());
    EXPECT_TRUE(provider_b.is_alive());
}

TEST_F(CoordinatorTest, UnifiedRegistrationMergesMappings) {
    SimConnectCoordinator coord;
    auto stub = std::make_unique<StubSimConnectClient>();
    auto* stub_ptr = stub.get();
    coord.set_client_for_testing(std::move(stub));

    SimConnectProvider provider(coord);
    coord.add_provider(&provider);

    auto input = make_coord_test_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build_mappings(input);

    coord.connect(&provider);

    // Check that RegisterNames was sent with merged vars
    std::string last_json = stub_ptr->last_request();
    EXPECT_FALSE(last_json.empty());
    EXPECT_NE(last_json.find("RegisterNames"), std::string::npos);
}
