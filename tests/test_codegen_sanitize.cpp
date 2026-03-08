/// Regression: device names with colons (from blueprint expansion) must be
/// sanitized to valid C++ identifiers in generated AOT code.

#include <gtest/gtest.h>
#include <regex>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"

using namespace an24;

// Helper: build a minimal circuit with blueprint-expanded device names
// (names like "simple_battery_1:bat" come from hierarchical blueprint expansion)
static auto make_colon_circuit() {
    struct Result {
        std::vector<DeviceInstance> devices;
        std::vector<Connection> connections;
        std::unordered_map<std::string, uint32_t> port_to_signal;
        uint32_t signal_count;
    };

    std::vector<DeviceInstance> devices;
    std::vector<std::pair<std::string, std::string>> conn_pairs;

    // Blueprint-expanded names with colons
    DeviceInstance ref;
    ref.name = "bp_1:gnd";
    ref.classname = "RefNode";
    ref.params = {{"value", "0"}};
    ref.ports["v"] = {PortDirection::Out};
    ref.domains = {Domain::Electrical};
    devices.push_back(ref);

    DeviceInstance bat;
    bat.name = "bp_1:bat";
    bat.classname = "Battery";
    bat.params = {{"v_nominal", "28"}, {"internal_r", "0.01"}, {"capacity", "100"}, {"charge", "100"}};
    bat.ports["v_out"] = {PortDirection::Out};
    bat.ports["v_in"] = {PortDirection::In};
    bat.domains = {Domain::Electrical};
    devices.push_back(bat);

    DeviceInstance bus;
    bus.name = "bp_1:main-bus";  // also has a hyphen
    bus.classname = "Bus";
    bus.ports["v"] = {PortDirection::InOut};
    bus.domains = {Domain::Electrical};
    devices.push_back(bus);

    DeviceInstance load;
    load.name = "bp_1:load.1";  // also has a dot
    load.classname = "Load";
    load.params = {{"resistance", "10"}};
    load.ports["v_in"] = {PortDirection::In};
    load.ports["v_out"] = {PortDirection::Out};
    load.domains = {Domain::Electrical};
    devices.push_back(load);

    conn_pairs.push_back({"bp_1:bat.v_in", "bp_1:gnd.v"});
    conn_pairs.push_back({"bp_1:bat.v_out", "bp_1:main-bus.v"});
    conn_pairs.push_back({"bp_1:main-bus.v", "bp_1:load.1.v_in"});
    conn_pairs.push_back({"bp_1:load.1.v_out", "bp_1:gnd.v"});

    auto sys = build_systems_dev(devices, conn_pairs);

    std::vector<Connection> connections;
    for (auto& c : conn_pairs) {
        connections.push_back({c.first, c.second});
    }

    return Result{devices, connections, sys.port_to_signal, sys.signal_count};
}

// Regex that matches a C++ identifier character that is NOT valid
// after a letter/digit position: colon, hyphen, dot
static bool has_bad_identifier(const std::string& code) {
    // Find all occurrences of : . - that appear between word characters
    // (i.e., inside what should be an identifier, not in ::, ->, etc.)
    // Specifically check for patterns like word:word or word.word or word-word
    // that aren't part of C++ syntax (::, ->, .)
    std::regex bad_pattern(R"(\b\w+[:]\w+\b)");
    std::sregex_iterator it(code.begin(), code.end(), bad_pattern);
    std::sregex_iterator end;
    for (; it != end; ++it) {
        std::string match = it->str();
        // Allow C++ :: scope operator and standard patterns
        if (match.find("::") != std::string::npos) continue;
        // This is a bad identifier like simple_battery_1:bat
        return true;
    }
    return false;
}

TEST(CodegenSanitize, DeviceNamesWithColonsAreValidIdentifiers) {
    auto [devices, connections, port_to_signal, signal_count] = make_colon_circuit();

    std::string header = CodeGen::generate_header(
        "test.json", devices, connections, port_to_signal, signal_count);
    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    // Colons in device names should be replaced with underscores
    EXPECT_FALSE(has_bad_identifier(header))
        << "Header contains device names with colons that aren't valid C++ identifiers";
    EXPECT_FALSE(has_bad_identifier(source))
        << "Source contains device names with colons that aren't valid C++ identifiers";

    // The sanitized names should be present
    EXPECT_NE(header.find("bp_1_bat"), std::string::npos)
        << "Sanitized name bp_1_bat not found in header";
    EXPECT_NE(header.find("bp_1_gnd"), std::string::npos)
        << "Sanitized name bp_1_gnd not found in header";
    EXPECT_NE(header.find("bp_1_main_bus"), std::string::npos)
        << "Sanitized name bp_1_main_bus not found in header (hyphen should become underscore)";
    EXPECT_NE(header.find("bp_1_load_1"), std::string::npos)
        << "Sanitized name bp_1_load_1 not found in header (dot should become underscore)";

    // Verify source also uses sanitized names in method bodies
    EXPECT_NE(source.find("bp_1_bat.solve_electrical"), std::string::npos)
        << "Source should use sanitized name in solve_electrical call";
    EXPECT_NE(source.find("bp_1_bat.inv_internal_r"), std::string::npos)
        << "Source should use sanitized name in pre_load";
}

TEST(CodegenSanitize, SanitizeNameFunction) {
    // Direct test of the sanitization rules
    // Colons, dots, and hyphens should all become underscores
    auto gen_and_check = [](const std::string& input, const std::string& expected_fragment) {
        // Create a minimal device with the given name and generate header
        std::vector<DeviceInstance> devices;
        DeviceInstance dev;
        dev.name = input;
        dev.classname = "RefNode";
        dev.ports["v"] = {PortDirection::Out};
        dev.domains = {Domain::Electrical};
        devices.push_back(dev);

        std::unordered_map<std::string, uint32_t> port_to_signal;
        port_to_signal[input + ".v"] = 0;

        std::string header = CodeGen::generate_header(
            "test.json", devices, {}, port_to_signal, 1);

        EXPECT_NE(header.find(expected_fragment), std::string::npos)
            << "Expected '" << expected_fragment << "' in header for input '" << input << "'";
    };

    gen_and_check("dev:sub", "dev_sub");
    gen_and_check("dev-name", "dev_name");
    gen_and_check("dev.part", "dev_part");
    gen_and_check("a:b-c.d", "a_b_c_d");
}
