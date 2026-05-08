#include "simconnect/simconnect_client.h"
#include "simconnect/simconnect_client_stub.h"
#include <gtest/gtest.h>

// =============================================================================
// SimConnect Stub Client Tests
// =============================================================================
// Verifies the stub implementation behaves correctly for test assertions.

// ==...== Connection Lifecycle ==...==

TEST(SimConnectStubTest, ConnectReturnsTrue) {
    auto client = create_simconnect_client();
    EXPECT_TRUE(client->connect());
    EXPECT_TRUE(client->is_connected());
}

TEST(SimConnectStubTest, DisconnectSetsNotConnected) {
    auto client = create_simconnect_client();
    client->connect();
    client->disconnect();
    EXPECT_FALSE(client->is_connected());
}

TEST(SimConnectStubTest, PollIsNoOp) {
    auto client = create_simconnect_client();
    client->connect();
    // Should not crash
    client->poll(0.016);
}

// ==...== Direct SimVar Access ==...==

TEST(SimConnectStubTest, ReadReturnsDefaultValue) {
    auto client = create_simconnect_client();
    auto val = client->read("AMBIENT TEMPERATURE", 0);
    EXPECT_FALSE(val.valid);
    EXPECT_FLOAT_EQ(val.value, 0.0f);
}

TEST(SimConnectStubTest, ReadReturnsSetValue) {
    auto client = create_simconnect_client();
    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    stub->set_sim_value("AMBIENT TEMPERATURE", 0, 15.0f);

    auto val = client->read("AMBIENT TEMPERATURE", 0);
    EXPECT_TRUE(val.valid);
    EXPECT_FLOAT_EQ(val.value, 15.0f);
}

TEST(SimConnectStubTest, WriteRecordsValue) {
    auto client = create_simconnect_client();
    client->write("ELECTRICAL MAIN BUS VOLTAGE", 0, 28.5f);

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    auto written = stub->written("ELECTRICAL MAIN BUS VOLTAGE", 0);
    ASSERT_TRUE(written.has_value());
    EXPECT_FLOAT_EQ(written.value(), 28.5f);
}

// ==...== Events ==...==

TEST(SimConnectStubTest, SendEventRecordsEvent) {
    auto client = create_simconnect_client();
    client->send_event("LANDING_LIGHTS_SET", 1);

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    const auto& events = stub->events_sent();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].first, "LANDING_LIGHTS_SET");
    EXPECT_EQ(events[0].second, 1u);
}

// ==...== CommBus ==...==

TEST(SimConnectStubTest, SendRequestStoresPayload) {
    auto client = create_simconnect_client();
    client->send_request(R"({"type":"read","vars":["AMBIENT TEMPERATURE"]})");

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    EXPECT_EQ(stub->last_request(), R"({"type":"read","vars":["AMBIENT TEMPERATURE"]})");
}

TEST(SimConnectStubTest, ResponseCallbackInvokedWithJson) {
    auto client = create_simconnect_client();
    std::vector<uint8_t> received;
    client->set_response_callback([&received](std::span<const uint8_t> payload) {
        received.assign(payload.begin(), payload.end());
    });

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    stub->trigger_mock_response(R"({"type":"data","AMBIENT TEMPERATURE":15.0})");
    std::string expected = R"({"type":"data","AMBIENT TEMPERATURE":15.0})";
    EXPECT_EQ(std::string(received.begin(), received.end()), expected);
}

TEST(SimConnectStubTest, ResponseCallbackInvokedWithBinary) {
    auto client = create_simconnect_client();
    std::vector<uint8_t> received;
    client->set_response_callback([&received](std::span<const uint8_t> payload) {
        received.assign(payload.begin(), payload.end());
    });

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    std::vector<uint8_t> binary_data = {0x41, 0x4E, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00};
    stub->trigger_mock_response(binary_data);
    EXPECT_EQ(received, binary_data);
}

TEST(SimConnectStubTest, SendBytesStoresBinaryPayload) {
    auto client = create_simconnect_client();
    std::vector<uint8_t> data = {0x41, 0x4E, 0x02, 0x01};
    client->send_bytes(data.data(), data.size());

    auto* stub = static_cast<StubSimConnectClient*>(client.get());
    EXPECT_EQ(stub->last_request_bytes(), data);
}

// ==...== Reset ==...==

TEST(SimConnectStubTest, ResetClearsAllState) {
    auto client = create_simconnect_client();
    auto* stub = static_cast<StubSimConnectClient*>(client.get());

    stub->set_sim_value("TEST", 0, 1.0f);
    stub->write("TEST", 0, 2.0f);
    stub->send_event("TEST_EVT", 1);
    stub->send_request("test");
    stub->reset();

    EXPECT_FALSE(client->read("TEST", 0).valid);
    EXPECT_FALSE(stub->written("TEST", 0).has_value());
    EXPECT_TRUE(stub->events_sent().empty());
    EXPECT_TRUE(stub->last_request().empty());
}
