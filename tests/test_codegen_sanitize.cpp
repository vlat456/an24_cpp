/// Regression: device names with colons (from blueprint expansion) must be
/// sanitized to valid C++ identifiers in generated AOT code.

#include <gtest/gtest.h>
#include <regex>
#include "core/solvers/aot/codegen.h"
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "test_execution_phases.h"
#include "jit_build_input_test_helper.h"

namespace {

const ComponentRegistry& test_registry() {
    static const ComponentRegistry registry = load_component_registry("library/");
    return registry;
}

ResolvedDevice resolve_codegen_device(DeviceInstance dev) {
    const ComponentSpec* spec = test_registry().get(dev.classname);
    if (!spec) {
        throw std::runtime_error("Missing test spec for " + dev.classname);
    }
    return resolve_component(dev, *spec);
}

} // namespace


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

    // Blueprint-expanded names with colons
    DeviceInstance ref;
    ref.name = "bp_1:gnd";
    ref.classname = "RefNode";
    ref.params = {{"value", "0"}};
    ref.ports["v"] = {bp2::Direction::Output};
    ref.spec = test_registry().get("RefNode");
    if (const ComponentSpec* spec = test_registry().get("RefNode")) {
        ref = resolve_device(ref, *spec);
    }
    devices.push_back(ref);

    DeviceInstance bat;
    bat.name = "bp_1:bat";
    bat.classname = "ElectricalSource";
    bat.params = {{"voltage", "28"}, {"resistance", "0.01"}};
    bat.ports["v_out"] = {bp2::Direction::Output};
    bat.ports["v_in"] = {bp2::Direction::Input};
    bat.spec = test_registry().get("ElectricalSource");
    if (const ComponentSpec* spec = test_registry().get("ElectricalSource")) {
        bat = resolve_device(bat, *spec);
    }
    devices.push_back(bat);

    DeviceInstance bus;
    bus.name = "bp_1:main-bus";  // also has a hyphen
    bus.classname = "Bus";
    bus.ports["v"] = {bp2::Direction::InOut};
    bus.spec = test_registry().get("Bus");
    if (const ComponentSpec* spec = test_registry().get("Bus")) {
        bus = resolve_device(bus, *spec);
    }
    devices.push_back(bus);

    DeviceInstance load;
    load.name = "bp_1:load.1";  // also has a dot
    load.classname = "Resistor";
    load.params = {{"conductance", "0.1"}};
    load.ports["v_in"] = {bp2::Direction::Input};
    load.ports["v_out"] = {bp2::Direction::Output};
    load.spec = test_registry().get("Resistor");
    if (const ComponentSpec* spec = test_registry().get("Resistor")) {
        load = resolve_device(load, *spec);
    }
    devices.push_back(load);

    // Signal groups representing the electrical nets
    std::vector<std::vector<std::string>> signal_groups = {
        // Net 0: Ground
        {"bp_1:bat.v_in", "bp_1:gnd.v", "bp_1:load.1.v_out"},
        // Net 1: Main bus
        {"bp_1:bat.v_out", "bp_1:main-bus.v", "bp_1:load.1.v_in"}
    };

    auto sys = build_systems_dev(make_jit_input(devices, signal_groups));

    std::vector<Connection> connections;
    for (const auto& group : signal_groups) {
        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                connections.push_back({group[i], group[j]});
            }
        }
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
    std::vector<ResolvedDevice> resolved_devices;
    resolved_devices.reserve(devices.size());
    for (auto& dev : devices) {
        resolved_devices.push_back(resolve_codegen_device(std::move(dev)));
    }

    std::string header = CodeGen::generate_header(
        "test.json", resolved_devices, connections, port_to_signal, signal_count);
    std::string source = CodeGen::generate_source(
        "test.h", resolved_devices, connections, port_to_signal, signal_count);

    // Colons in device names should be replaced with underscores
    EXPECT_FALSE(has_bad_identifier(header))
        << "Header contains device names with colons that aren't valid C++ identifiers";
    EXPECT_FALSE(has_bad_identifier(source))
        << "Source contains device names with colons that aren't valid C++ identifiers";

    // The sanitized names should be present
    // ':' → '_', '-' → '_DASH_', '.' → '_DOT_'
    EXPECT_NE(header.find("bp_1_bat"), std::string::npos)
        << "Sanitized name bp_1_bat not found in header";
    EXPECT_NE(header.find("bp_1_gnd"), std::string::npos)
        << "Sanitized name bp_1_gnd not found in header";
    EXPECT_NE(header.find("bp_1_main_DASH_bus"), std::string::npos)
        << "Sanitized name bp_1_main_DASH_bus not found in header (hyphen should become _DASH_)";
    EXPECT_NE(header.find("bp_1_load_DOT_1"), std::string::npos)
        << "Sanitized name bp_1_load_DOT_1 not found in header (dot should become _DOT_)";

    // Verify source also uses sanitized names in method bodies
    EXPECT_NE(source.find("bp_1_bat.execute"), std::string::npos)
        << "Source should use sanitized name in execute call";
    EXPECT_NE(source.find("bp_1_bat.pre_load"), std::string::npos)
        << "Source should use sanitized name in pre_load";
}

TEST(CodegenSanitize, SanitizeNameFunction) {
    // Direct test of the sanitization rules
    // Colons, dots, and hyphens should all become underscores
    auto gen_and_check = [](const std::string& input, const std::string& expected_fragment) {
        // Create a minimal device with the given name and generate header
        DeviceInstance dev;
        dev.name = input;
        dev.classname = "RefNode";
        dev.ports["v"] = {bp2::Direction::Output};
        dev.spec = test_registry().get("RefNode");
        std::vector<ResolvedDevice> resolved_devices;
        resolved_devices.push_back(resolve_codegen_device(std::move(dev)));

        std::unordered_map<std::string, uint32_t> port_to_signal;
        port_to_signal[input + ".v"] = 0;

        std::string header = CodeGen::generate_header(
            "test.json", resolved_devices, {}, port_to_signal, 1);

        EXPECT_NE(header.find(expected_fragment), std::string::npos)
            << "Expected '" << expected_fragment << "' in header for input '" << input << "'";
    };

    gen_and_check("dev:sub", "dev_sub");
    gen_and_check("dev-name", "dev_DASH_name");
    gen_and_check("dev.part", "dev_DOT_part");
    gen_and_check("a:b-c.d", "a_b_DASH_c_DOT_d");
}

TEST(CodegenSanitize, NoCollisionBetweenDotAndDashAndColon) {
    // Regression: previously "engine.temp", "engine:temp", and "engine-temp"
    // all collapsed to "engine_temp", causing C++ redefinition errors.
    // With the new scheme: '.' → _DOT_, '-' → _DASH_, ':' → '_'
    // they produce distinct identifiers.

    auto sanitize_via_codegen = [](const std::string& name) -> std::string {
        DeviceInstance dev;
        dev.name = name;
        dev.classname = "RefNode";
        dev.ports["v"] = {bp2::Direction::Output};
        dev.spec = test_registry().get("RefNode");
        std::vector<ResolvedDevice> resolved_devices;
        resolved_devices.push_back(resolve_codegen_device(std::move(dev)));

        std::unordered_map<std::string, uint32_t> port_to_signal;
        port_to_signal[name + ".v"] = 0;

        return CodeGen::generate_header("test.json", resolved_devices, {}, port_to_signal, 1);
    };

    std::string h_dot   = sanitize_via_codegen("engine.temp");
    std::string h_colon = sanitize_via_codegen("engine:temp");
    std::string h_dash  = sanitize_via_codegen("engine-temp");

    // Each should produce a unique identifier
    EXPECT_NE(h_dot.find("engine_DOT_temp"), std::string::npos)
        << "engine.temp should sanitize to engine_DOT_temp";
    EXPECT_NE(h_colon.find("engine_temp"), std::string::npos)
        << "engine:temp should sanitize to engine_temp";
    EXPECT_NE(h_dash.find("engine_DASH_temp"), std::string::npos)
        << "engine-temp should sanitize to engine_DASH_temp";

    // Verify they are all different: _DOT_ vs _ vs _DASH_
    // The key guarantee is that "engine.temp" does NOT produce "engine_temp"
    EXPECT_EQ(h_dot.find("RefNode<AotProvider<Binding<PortNames::v, 0>>> engine_temp;"), std::string::npos)
        << "engine.temp must NOT collide with engine:temp (both producing 'engine_temp')";
}
