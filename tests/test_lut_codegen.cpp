#include <gtest/gtest.h>
#include <optional>

#include "core/solvers/aot/codegen.h"
#include "io/json/component_registry_json_loader.h"
#include "core/registry/component_resolution.h"
#include "test_execution_phases.h"


// =============================================================================
// Static registry for test helpers — keeps specs alive
// =============================================================================

static const ComponentRegistry& lut_test_registry() {
    static const ComponentRegistry registry = load_component_registry("library/");
    return registry;
}

// Helper: check if device class is visual_only in presentation
static bool is_device_visual_only(const std::string& classname) {
    const auto& reg = lut_test_registry();
    if (auto* pres = reg.get_presentation(classname)) {
        return pres->visual_only;
    }
    return false;
}

static ResolvedDevice resolve_test_device(DeviceInstance dev) {
    const ComponentSpec* spec = lut_test_registry().get(dev.classname);
    if (!spec) {
        throw std::runtime_error("Missing test spec for " + dev.classname);
    }
    // Skip visual-only devices - same as elaboration boundary filtering
    if (is_device_visual_only(dev.classname)) {
        // Return empty device with empty name to signal "skip"
        return {};
    }
    const auto& reg = lut_test_registry();
    return resolve_component(dev, *spec);
}

// Helper to add a device, skipping visual-only ones and assigning ports
static void add_device(
    std::vector<ResolvedDevice>& devices,
    std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t& next_sig,
    DeviceInstance dev)
{
    auto resolved = resolve_test_device(std::move(dev));
    if (!resolved.name.empty()) {
        for (const auto& [port_name, port] : resolved.ports) {
            (void)port;
            port_to_signal[resolved.name + "." + port_name] = next_sig++;
        }
        devices.push_back(resolved);
    }
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
    return dev;
}

static auto make_ref_node() {
    DeviceInstance dev;
    dev.name = "gnd";
    dev.classname = "RefNode";
    dev.ports["v"] = {bp2::Direction::Output, PortType::V, std::nullopt};
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
    auto resolved = resolve_test_device(std::move(gnd));
    if (!resolved.name.empty()) {
        s.port_to_signal["gnd.v"] = next_sig++;
        s.devices.push_back(resolved);
    }

    for (auto& dev : extra_devices) {
        auto resolved = resolve_test_device(std::move(dev));
        if (resolved.name.empty()) {
            continue; // Skip visual-only
        }
        for (const auto& [port_name, port] : dev.ports) {
            s.port_to_signal[dev.name + "." + port_name] = next_sig++;
        }
        s.devices.push_back(resolved);
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
        "test.h", setup.devices,
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
        "test.h", setup.devices,
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
        "test.h", setup.devices,
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
    bat.classname = "ElectricalSource";
    bat.params["voltage"] = "28.0";
    bat.params["resistance"] = "0.1";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};

    auto setup = make_setup({std::move(bat)});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices,
        setup.port_to_signal, setup.signal_count);

    EXPECT_EQ(source.find("lut_keys_data"), std::string::npos)
        << "Should not emit LUT arena code when no LUTs exist";
    EXPECT_EQ(source.find("lut_vals_data"), std::string::npos);
}

TEST(LUTCodegen, EmptyTableParam_StillEmitsZeroSize) {
    auto setup = make_setup({make_lut_device("empty_lut", "")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices,
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
        "test.h", setup.devices,
        setup.port_to_signal, setup.signal_count);

    // Should have a comment showing total float count
    EXPECT_NE(source.find("5 floats total"), std::string::npos)
        << "Arena comment should show total float count (3 + 2 = 5)";
}

TEST(LUTCodegen, SingleEntryTable) {
    auto setup = make_setup({make_lut_device("single_lut", "42:99")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices,
        setup.port_to_signal, setup.signal_count);

    EXPECT_NE(source.find("single_lut.table_offset = 0"), std::string::npos);
    EXPECT_NE(source.find("single_lut.table_size = 1"), std::string::npos);
    EXPECT_NE(source.find("42.0f"), std::string::npos);
    EXPECT_NE(source.find("99.0f"), std::string::npos);
}

TEST(LUTCodegen, NegativeKeyValues_EncodedCorrectly) {
    auto setup = make_setup({make_lut_device("neg_lut", "-10:-5; 0:0; 10:5")});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices,
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

    auto setup = make_setup({std::move(lut)});

    std::string source = CodeGen::generate_source(
        "test.h", setup.devices,
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
    bat.classname = "ElectricalSource";
    bat.params["voltage"] = "28.0";
    bat.params["resistance"] = "0.1";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // visual_only device — filtered at resolution boundary, won't be in device list
    DeviceInstance grp;
    grp.name = "grp1";
    grp.classname = "Group";
    auto resolved_grp = resolve_test_device(std::move(grp));
    // Should be empty since Group is visual_only
    if (!resolved_grp.name.empty()) {
        s.devices.push_back(resolved_grp);
    }

    s.signal_count = next_sig;

    std::string header = CodeGen::generate_header(
        "test.json", s.devices,
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
    bat.classname = "ElectricalSource";
    bat.params["voltage"] = "24.0";
    bat.params["resistance"] = "0.05";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // visual_only device - filtered at resolution boundary
    DeviceInstance grp;
    grp.name = "grp1";
    grp.classname = "Group";
    auto resolved_grp = resolve_test_device(std::move(grp));
    if (!resolved_grp.name.empty()) {
        s.devices.push_back(resolved_grp);
    }

    s.signal_count = next_sig;

    std::string source = CodeGen::generate_source(
        "test.h", s.devices,
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
    bat.classname = "ElectricalSource";
    bat.params["voltage"] = "28.0";
    bat.params["resistance"] = "0.1";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // Text visual_only device — filtered at resolution boundary
    DeviceInstance txt;
    txt.name = "txt1";
    txt.classname = "Text";
    txt.params["text"] = "annotation";
    txt.params["font_size"] = "large";
    auto resolved_txt = resolve_test_device(std::move(txt));
    if (!resolved_txt.name.empty()) {
        s.devices.push_back(resolved_txt);
    }

    s.signal_count = next_sig;

    std::string header = CodeGen::generate_header(
        "test.json", s.devices,
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
    bat.classname = "ElectricalSource";
    bat.params["voltage"] = "24.0";
    bat.params["resistance"] = "0.05";
    bat.ports["v_in"]  = {bp2::Direction::Input,  PortType::V, std::nullopt};
    bat.ports["v_out"] = {bp2::Direction::Output, PortType::V, std::nullopt};
    s.port_to_signal["bat.v_in"]  = next_sig++;
    s.port_to_signal["bat.v_out"] = next_sig++;
    s.devices.push_back(resolve_test_device(std::move(bat)));

    // Text visual_only device - filtered at resolution boundary
    DeviceInstance txt;
    txt.name = "txt1";
    txt.classname = "Text";
    txt.params["text"] = "note";
    txt.params["font_size"] = "medium";
    auto resolved_txt = resolve_test_device(std::move(txt));
    if (!resolved_txt.name.empty()) {
        s.devices.push_back(resolved_txt);
    }

    s.signal_count = next_sig;

    std::string source = CodeGen::generate_source(
        "test.h", s.devices,
        s.port_to_signal, s.signal_count);

    EXPECT_NE(source.find("bat"), std::string::npos)
        << "Normal device 'bat' should be in generated source";
    EXPECT_EQ(source.find("txt1"), std::string::npos)
        << "visual_only Text device 'txt1' must NOT appear in generated source";
    EXPECT_EQ(source.find("note"), std::string::npos)
        << "Text param content must NOT appear in generated source";
}
