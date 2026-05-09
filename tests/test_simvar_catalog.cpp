#include "simconnect/simvar_catalog.h"
#include "simconnect/wire_protocol.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// =============================================================================
// SimVarCatalog Tests
// =============================================================================

class SimVarCatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        SimVarCatalog::reset_instance();
    }
    void TearDown() override {
        SimVarCatalog::reset_instance();
    }
};

TEST_F(SimVarCatalogTest, EmptyCatalogReturnsNoResults) {
    auto& cat = SimVarCatalog::instance();
    EXPECT_EQ(cat.size(), 0u);
    EXPECT_EQ(cat.avar_count(), 0u);
    EXPECT_EQ(cat.lvar_count(), 0u);

    auto results = cat.find("anything");
    EXPECT_TRUE(results.empty());
}

TEST_F(SimVarCatalogTest, LoadBundledAVarsFromJson) {
    auto& cat = SimVarCatalog::instance();

    std::string const json = R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32", "description": "Outside air temperature"},
            {"name": "ENG RPM:1", "unit": "Rpm", "val_type": "Float32", "description": "Engine 1 RPM"},
            {"name": "ENG RPM:2", "unit": "Rpm", "val_type": "Float32", "description": "Engine 2 RPM"},
            {"name": "INDICATED ALTITUDE", "unit": "Feet", "val_type": "Float32", "description": "Barometric altitude"},
            {"name": "AIRSPEED INDICATED", "unit": "Knots", "val_type": "Float32", "description": "Indicated airspeed"}
        ]
    })";

    ASSERT_TRUE(cat.load_bundled_from_string(json));
    EXPECT_EQ(cat.avar_count(), 5u);
    EXPECT_EQ(cat.size(), 5u);
}

TEST_F(SimVarCatalogTest, FindByExactName) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"},
            {"name": "ENG RPM:1", "unit": "Rpm", "val_type": "Float32"}
        ]
    })");

    auto results = cat.find("AMBIENT TEMPERATURE");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "AMBIENT TEMPERATURE");
    EXPECT_EQ(results[0].var_type, VarType::AVar);
    EXPECT_EQ(results[0].val_type, ValType::Float32);
}

TEST_F(SimVarCatalogTest, FindIsCaseInsensitive) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"}
        ]
    })");

    EXPECT_EQ(cat.find("ambient temperature").size(), 1u);
    EXPECT_EQ(cat.find("Ambient Temperature").size(), 1u);
    EXPECT_EQ(cat.find("AMBIENT").size(), 1u);
}

TEST_F(SimVarCatalogTest, FindBySubstring) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"},
            {"name": "ENG RPM:1", "unit": "Rpm", "val_type": "Float32"},
            {"name": "ENG RPM:2", "unit": "Rpm", "val_type": "Float32"},
            {"name": "INDICATED ALTITUDE", "unit": "Feet", "val_type": "Float32"},
            {"name": "AIRSPEED INDICATED", "unit": "Knots", "val_type": "Float32"}
        ]
    })");

    auto rpm_results = cat.find("RPM");
    ASSERT_EQ(rpm_results.size(), 2u);
    EXPECT_EQ(rpm_results[0].name, "ENG RPM:1");
    EXPECT_EQ(rpm_results[1].name, "ENG RPM:2");

    auto alt_results = cat.find("ALT");
    ASSERT_EQ(alt_results.size(), 1u);
    EXPECT_EQ(alt_results[0].name, "INDICATED ALTITUDE");
}

TEST_F(SimVarCatalogTest, FilterByVarType) {
    auto& cat = SimVarCatalog::instance();

    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"},
            {"name": "ENG RPM:1", "unit": "Rpm", "val_type": "Float32"}
        ]
    })");

    cat.add_lvar("AN24_CUSTOM_VALVE", ValType::Float32);
    cat.add_lvar("AN24_SWITCH_LANDING_LIGHT", ValType::Bool);

    auto avar_results = cat.find("", VarType::AVar);
    ASSERT_EQ(avar_results.size(), 2u);

    auto lvar_results = cat.find("", VarType::LVar);
    ASSERT_EQ(lvar_results.size(), 2u);

    auto filtered_lvars = cat.find("SWITCH", VarType::LVar);
    ASSERT_EQ(filtered_lvars.size(), 1u);
    EXPECT_EQ(filtered_lvars[0].name, "AN24_SWITCH_LANDING_LIGHT");
}

TEST_F(SimVarCatalogTest, MergedLvarsWithAvars) {
    auto& cat = SimVarCatalog::instance();

    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"}
        ]
    })");

    cat.add_lvar("AN24_TEST_VAR", ValType::Float32);

    auto all = cat.find("");
    ASSERT_EQ(all.size(), 2u);

    EXPECT_EQ(cat.avar_count(), 1u);
    EXPECT_EQ(cat.lvar_count(), 1u);
    EXPECT_EQ(cat.size(), 2u);
}

TEST_F(SimVarCatalogTest, AddLvarsFromVector) {
    auto& cat = SimVarCatalog::instance();

    std::vector<SimVarCatalog::Entry> lvars;
    lvars.push_back({"AN24_VAR_A", VarType::LVar, ValType::Float32, "", ""});
    lvars.push_back({"AN24_VAR_B", VarType::LVar, ValType::Bool, "", ""});

    cat.add_lvars(lvars);

    EXPECT_EQ(cat.lvar_count(), 2u);
    EXPECT_EQ(cat.find("VAR_A", VarType::LVar).size(), 1u);
}

TEST_F(SimVarCatalogTest, InvalidJsonReturnsFalse) {
    auto& cat = SimVarCatalog::instance();
    EXPECT_FALSE(cat.load_bundled_from_string("not valid json"));
}

TEST_F(SimVarCatalogTest, MissingVersionKeyReturnsFalse) {
    auto& cat = SimVarCatalog::instance();
    EXPECT_FALSE(cat.load_bundled_from_string(R"({"avars": []})"));
}

TEST_F(SimVarCatalogTest, EmptyFilterReturnsAll) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "A", "unit": "u1", "val_type": "Float32"},
            {"name": "B", "unit": "u2", "val_type": "Float32"},
            {"name": "C", "unit": "u3", "val_type": "Float32"}
        ]
    })");

    auto all = cat.find("");
    ASSERT_EQ(all.size(), 3u);

    auto all_avar = cat.find("", VarType::AVar);
    ASSERT_EQ(all_avar.size(), 3u);
}

TEST_F(SimVarCatalogTest, FindBySubstringInMiddle) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "ENG RPM:1", "unit": "Rpm", "val_type": "Float32"},
            {"name": "ENG RPM:2", "unit": "Rpm", "val_type": "Float32"},
            {"name": "INDICATED ALTITUDE", "unit": "Feet", "val_type": "Float32"}
        ]
    })");

    auto results = cat.find("RPM");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].name, "ENG RPM:1");
    EXPECT_EQ(results[1].name, "ENG RPM:2");

    results = cat.find("ALT");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "INDICATED ALTITUDE");
}

TEST_F(SimVarCatalogTest, EmptyAvarsArrayIsValid) {
    auto& cat = SimVarCatalog::instance();
    ASSERT_TRUE(cat.load_bundled_from_string(R"({"version": 1, "avars": []})"));
    EXPECT_EQ(cat.avar_count(), 0u);
    EXPECT_EQ(cat.size(), 0u);
}

TEST_F(SimVarCatalogTest, WrongVersionReturnsFalse) {
    auto& cat = SimVarCatalog::instance();
    EXPECT_FALSE(cat.load_bundled_from_string(R"({"version": 2, "avars": []})"));
    EXPECT_FALSE(cat.load_bundled_from_string(R"({"version": 0, "avars": []})"));
}

TEST_F(SimVarCatalogTest, ClearLvars) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"}
        ]
    })");
    cat.add_lvar("AN24_TEST", ValType::Float32);
    EXPECT_EQ(cat.size(), 2u);
    EXPECT_EQ(cat.lvar_count(), 1u);

    cat.clear_lvars();
    EXPECT_EQ(cat.lvar_count(), 0u);
    EXPECT_EQ(cat.avar_count(), 1u);
    EXPECT_EQ(cat.size(), 1u);
}

TEST_F(SimVarCatalogTest, LoadBundledFromFile) {
    auto& cat = SimVarCatalog::instance();

    std::string const json = R"({
        "version": 1,
        "avars": [
            {"name": "TEST_VAR_FROM_FILE", "unit": "Feet", "val_type": "Float32"}
        ]
    })";

    std::string const tmp_path = "/tmp/test_simvar_catalog.json";
    {
        std::ofstream f(tmp_path);
        f << json;
    }

    ASSERT_TRUE(cat.load_bundled(tmp_path));
    EXPECT_EQ(cat.avar_count(), 1u);
    EXPECT_EQ(cat.find("TEST_VAR_FROM_FILE").size(), 1u);

    std::remove(tmp_path.c_str());
}

TEST_F(SimVarCatalogTest, ResetClearsState) {
    auto& cat = SimVarCatalog::instance();
    cat.load_bundled_from_string(R"({
        "version": 1,
        "avars": [
            {"name": "AMBIENT TEMPERATURE", "unit": "Celsius", "val_type": "Float32"}
        ]
    })");
    cat.add_lvar("AN24_TEST", ValType::Float32);
    EXPECT_EQ(cat.size(), 2u);

    SimVarCatalog::reset_instance();
    auto& cat2 = SimVarCatalog::instance();
    EXPECT_EQ(cat2.size(), 0u);
}
