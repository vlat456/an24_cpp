#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"

using namespace an24;

// =============================================================================
// Helpers: construct LUT device instances for codegen tests
// =============================================================================

static auto make_lut_device(const std::string& name, const std::string& table) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = "LUT";
    dev.ports["input"]  = {PortDirection::In,  PortType::Any, std::nullopt};
    dev.ports["output"] = {PortDirection::Out, PortType::Any, std::nullopt};
    dev.params["table"] = table;
    return dev;
}

static auto make_ref_node() {
    DeviceInstance dev;
    dev.name = "gnd";
    dev.classname = "RefNode";
    dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
    return dev;
}

struct CodegenSetup {
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t signal_count;
};

static CodegenSetup make_setup(std::vector<DeviceInstance> extra_devices) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    // Always need a RefNode
    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v_out"] = next_sig++;
    s.devices.push_back(std::move(gnd));

    for (auto& dev : extra_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            s.port_to_signal[dev.name + "." + port_name] = next_sig++;
        }
        s.devices.push_back(std::move(dev));
    }
    s.signal_count = next_sig;
    return s;
}

// =============================================================================
// AOT codegen: LUT constructor emits table_offset / table_size (not "table")
// =============================================================================

TEST(LUTCodegen, Constructor_EmitsOffsetAndSize_NotTableString) {
    auto setup = make_setup({make_lut_device("my_lut", "0:0; 100:50; 200:100")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // Must contain integer offset/size assignments
    EXPECT_NE(source.find("my_lut.table_offset = 0"), std::string::npos)
        << "Constructor must set table_offset for LUT device";
    EXPECT_NE(source.find("my_lut.table_size = 3"), std::string::npos)
        << "Constructor must set table_size for LUT device (3 entries)";

    // Must NOT contain string table assignment
    EXPECT_EQ(source.find("my_lut.table ="), std::string::npos)
        << "Constructor must NOT emit string 'table =' param for LUT";
}

TEST(LUTCodegen, PreLoad_EmitsStaticArenaArrays) {
    auto setup = make_setup({make_lut_device("my_lut", "10:20; 30:40; 50:60")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // pre_load must contain static const float arrays with the data
    EXPECT_NE(source.find("lut_keys_data[]"), std::string::npos)
        << "pre_load must emit static lut_keys_data array";
    EXPECT_NE(source.find("lut_vals_data[]"), std::string::npos)
        << "pre_load must emit static lut_vals_data array";

    // Must contain the actual key values
    EXPECT_NE(source.find("10f"), std::string::npos);
    EXPECT_NE(source.find("30f"), std::string::npos);
    EXPECT_NE(source.find("50f"), std::string::npos);

    // Must assign to g_state arena
    EXPECT_NE(source.find("g_state->lut_keys.assign"), std::string::npos);
    EXPECT_NE(source.find("g_state->lut_values.assign"), std::string::npos);
}

TEST(LUTCodegen, MultipleLUTs_OffsetsIncrement) {
    auto setup = make_setup({
        make_lut_device("lut_a", "0:0; 10:10"),       // 2 entries -> offset 0
        make_lut_device("lut_b", "0:0; 5:5; 10:10"),  // 3 entries -> offset 2
    });

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // lut_a: offset=0, size=2
    EXPECT_NE(source.find("lut_a.table_offset = 0"), std::string::npos);
    EXPECT_NE(source.find("lut_a.table_size = 2"), std::string::npos);

    // lut_b: offset=2, size=3
    EXPECT_NE(source.find("lut_b.table_offset = 2"), std::string::npos);
    EXPECT_NE(source.find("lut_b.table_size = 3"), std::string::npos);
}

TEST(LUTCodegen, NoLUTs_NoArenaCode) {
    // A circuit with no LUTs should not generate arena code
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.params["v_nominal"] = "28";
    bat.params["internal_r"] = "0.1";
    bat.ports["v_in"]  = {PortDirection::In,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};

    auto setup = make_setup({std::move(bat)});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    EXPECT_EQ(source.find("lut_keys_data"), std::string::npos)
        << "Should not emit LUT arena code when no LUTs exist";
    EXPECT_EQ(source.find("lut_vals_data"), std::string::npos);
}

TEST(LUTCodegen, EmptyTableParam_StillEmitsZeroSize) {
    auto setup = make_setup({make_lut_device("empty_lut", "")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // LUT with empty table: size should be 0
    EXPECT_NE(source.find("empty_lut.table_size = 0"), std::string::npos)
        << "Empty table should emit table_size = 0";
}

TEST(LUTCodegen, ArenaComment_ShowsTotalFloats) {
    auto setup = make_setup({
        make_lut_device("lut1", "0:0; 10:10; 20:20"),  // 3
        make_lut_device("lut2", "0:0; 5:5"),            // 2
    });

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // Should have a comment showing total float count
    EXPECT_NE(source.find("5 floats total"), std::string::npos)
        << "Arena comment should show total float count (3 + 2 = 5)";
}

TEST(LUTCodegen, SingleEntryTable) {
    auto setup = make_setup({make_lut_device("single_lut", "42:99")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    EXPECT_NE(source.find("single_lut.table_offset = 0"), std::string::npos);
    EXPECT_NE(source.find("single_lut.table_size = 1"), std::string::npos);
    EXPECT_NE(source.find("42f"), std::string::npos);
    EXPECT_NE(source.find("99f"), std::string::npos);
}

TEST(LUTCodegen, NegativeKeyValues_EncodedCorrectly) {
    auto setup = make_setup({make_lut_device("neg_lut", "-10:-5; 0:0; 10:5")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // Negative values should appear with minus sign
    EXPECT_NE(source.find("-10f"), std::string::npos);
    EXPECT_NE(source.find("-5f"), std::string::npos);
}

// =============================================================================
// Regression: codegen skips generic param loop for LUT
// =============================================================================

TEST(LUTCodegen, GenericParamLoop_SkippedForLUT) {
    // A LUT with extra params besides "table" — codegen must skip them all
    DeviceInstance lut;
    lut.name = "test_lut";
    lut.classname = "LUT";
    lut.ports["input"]  = {PortDirection::In,  PortType::Any, std::nullopt};
    lut.ports["output"] = {PortDirection::Out, PortType::Any, std::nullopt};
    lut.params["table"] = "0:0; 100:100";

    auto setup = make_setup({std::move(lut)});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // Must not contain any "test_lut.table =" (string assignment)
    EXPECT_EQ(source.find("test_lut.table ="), std::string::npos);
    // Must contain the integer assignments instead
    EXPECT_NE(source.find("test_lut.table_offset"), std::string::npos);
    EXPECT_NE(source.find("test_lut.table_size"), std::string::npos);
}
