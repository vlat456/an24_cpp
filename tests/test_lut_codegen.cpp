#include <gtest/gtest.h>
#include "core/solvers/aot/codegen.h"
#include "json_parser/json_parser.h"
#include "test_execution_phases.h"


// =============================================================================
// Static registry for test helpers — keeps specs alive
// =============================================================================

static const ComponentRegistry& lut_test_registry() {
    static const ComponentRegistry registry = load_component_registry("library/");
    return registry;
}

static ResolvedDevice resolve_test_device(DeviceInstance dev) {
    const ComponentSpec* spec = lut_test_registry().get(dev.classname);
    if (!spec) {
        throw std::runtime_error("Missing test spec for " + dev.classname);
    }
    return resolve_component(dev, *spec);
}


// =============================================================================
// Helpers: construct LUT device instances for codegen tests
// =============================================================================

static auto make_lut_device(const std::string& name, const std::string& table) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = "LUT";
    dev.ports["input"]  = {bp2::Direction::Input,  PortType::Any, std::nullopt};
    dev.ports["output"] = {bp2::Direction::Output, PortType::Any, std::nullopt};
    dev.params["table"] = table;
    dev.spec = lut_test_registry().get("LUT");
    return dev;
}

static auto make_ref_node() {
    DeviceInstance dev;
    dev.name = "gnd";
    dev.classname = "RefNode";
    dev.ports["v"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    dev.spec = lut_test_registry().get("RefNode");
    return dev;
}

struct CodegenSetup {
    std::vector<ResolvedDevice> devices;
    std::vector<Connection> connections;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t signal_count;
};

static CodegenSetup make_setup(std::vector<DeviceInstance> extra_devices) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    // Always need a RefNode
    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(gnd)));

    for (auto& dev : extra_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            s.port_to_signal[dev.name + "." + port_name] = next_sig++;
        }
        s.devices.push_back(resolve_test_device(std::move(dev)));
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

    // Must contain the actual key values (format_float produces "10.0", "30.0", "50.0")
    EXPECT_NE(source.find("10.0f"), std::string::npos);
    EXPECT_NE(source.find("30.0f"), std::string::npos);
    EXPECT_NE(source.find("50.0f"), std::string::npos);

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
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    bat.spec = lut_test_registry().get("Battery");

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
    EXPECT_NE(source.find("42.0f"), std::string::npos);
    EXPECT_NE(source.find("99.0f"), std::string::npos);
}

TEST(LUTCodegen, NegativeKeyValues_EncodedCorrectly) {
    auto setup = make_setup({make_lut_device("neg_lut", "-10:-5; 0:0; 10:5")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices, setup.connections,
        setup.port_to_signal, setup.signal_count);

    // Negative values should appear with minus sign (format_float produces "-10.0", "-5.0")
    EXPECT_NE(source.find("-10.0f"), std::string::npos);
    EXPECT_NE(source.find("-5.0f"), std::string::npos);
}

// =============================================================================
// Regression: codegen skips generic param loop for LUT
// =============================================================================

TEST(LUTCodegen, GenericParamLoop_SkippedForLUT) {
    // A LUT with extra params besides "table" — codegen must skip them all
    DeviceInstance lut;
    lut.name = "test_lut";
    lut.classname = "LUT";
    lut.ports["input"]  = {bp2::Direction::Input,  PortType::Any, std::nullopt};
    lut.ports["output"] = {bp2::Direction::Output, PortType::Any, std::nullopt};
    lut.params["table"] = "0:0; 100:100";
    lut.spec = lut_test_registry().get("LUT");

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

// =============================================================================
// visual_only devices must NOT leak into AOT codegen
// =============================================================================

TEST(AOTCodegen, VisualOnly_FilteredFromHeader) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(gnd)));

    // Normal device
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    bat.spec = lut_test_registry().get("Battery");
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // visual_only device — must NOT appear in generated code
    DeviceInstance grp;
    grp.name = "grp1";
    grp.classname = "Group";
    grp.spec = lut_test_registry().get("Group");
    s.devices.push_back(resolve_test_device(std::move(grp)));

    s.signal_count = next_sig;

    std::string header = CodeGen::generate_header(
        "test.json", s.devices, s.connections,
        s.port_to_signal, s.signal_count);

    EXPECT_NE(header.find("bat"), std::string::npos)
        << "Normal device 'bat' should be in generated header";
    EXPECT_EQ(header.find("grp1"), std::string::npos)
        << "visual_only device 'grp1' must NOT appear in generated header";
    EXPECT_EQ(header.find("Group"), std::string::npos)
        << "visual_only classname 'Group' must NOT appear in generated header";
}

TEST(AOTCodegen, VisualOnly_FilteredFromSource) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(gnd)));

    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    bat.params["emf"] = "24.0";
    bat.params["internal_r"] = "0.05";
    bat.spec = lut_test_registry().get("Battery");
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // visual_only device
    DeviceInstance grp;
    grp.name = "grp1";
    grp.classname = "Group";
    grp.spec = lut_test_registry().get("Group");
    s.devices.push_back(resolve_test_device(std::move(grp)));

    s.signal_count = next_sig;

    std::string source = CodeGen::generate_source(
        "test.h", s.devices, s.connections,
        s.port_to_signal, s.signal_count);

    EXPECT_NE(source.find("bat"), std::string::npos)
        << "Normal device 'bat' should be in generated source";
    EXPECT_EQ(source.find("grp1"), std::string::npos)
        << "visual_only device 'grp1' must NOT appear in generated source";
    EXPECT_EQ(source.find("Group"), std::string::npos)
        << "visual_only classname 'Group' must NOT appear in generated source";
}

// =============================================================================
// Text visual_only must NOT leak into AOT codegen
// =============================================================================

TEST(AOTCodegen, Text_VisualOnly_FilteredFromHeader) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(gnd)));

    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    bat.spec = lut_test_registry().get("Battery");
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // Text visual_only device — must NOT appear in generated code
    DeviceInstance txt;
    txt.name = "txt1";
    txt.classname = "Text";
    txt.spec = lut_test_registry().get("Text");
    txt.params["text"] = "annotation";
    txt.params["font_size"] = "large";
    s.devices.push_back(resolve_test_device(std::move(txt)));

    s.signal_count = next_sig;

    std::string header = CodeGen::generate_header(
        "test.json", s.devices, s.connections,
        s.port_to_signal, s.signal_count);

    EXPECT_NE(header.find("bat"), std::string::npos)
        << "Normal device 'bat' should be in generated header";
    EXPECT_EQ(header.find("txt1"), std::string::npos)
        << "visual_only Text device 'txt1' must NOT appear in generated header";
    EXPECT_EQ(header.find("annotation"), std::string::npos)
        << "Text param content must NOT appear in generated header";
}

TEST(AOTCodegen, Text_VisualOnly_FilteredFromSource) {
    CodegenSetup s;
    uint32_t next_sig = 0;

    auto gnd = make_ref_node();
    s.port_to_signal["gnd.v"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(gnd)));

    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    bat.params["emf"] = "24.0";
    bat.params["internal_r"] = "0.05";
    bat.spec = lut_test_registry().get("Battery");
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // Text visual_only device
    DeviceInstance txt;
    txt.name = "txt1";
    txt.classname = "Text";
    txt.spec = lut_test_registry().get("Text");
    txt.params["text"] = "note";
    txt.params["font_size"] = "medium";
    s.devices.push_back(resolve_test_device(std::move(txt)));

    s.signal_count = next_sig;

    std::string source = CodeGen::generate_source(
        "test.h", s.devices, s.connections,
        s.port_to_signal, s.signal_count);

    EXPECT_NE(source.find("bat"), std::string::npos)
        << "Normal device 'bat' should be in generated source";
    EXPECT_EQ(source.find("txt1"), std::string::npos)
        << "visual_only Text device 'txt1' must NOT appear in generated source";
    EXPECT_EQ(source.find("note"), std::string::npos)
        << "Text param content must NOT appear in generated source";
}
