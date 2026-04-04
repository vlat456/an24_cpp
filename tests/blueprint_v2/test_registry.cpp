#include <gtest/gtest.h>
#include "blueprint_v2/registry/type_registry.h"
#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"

TEST(TypeRegistry, Placeholder) {
    EXPECT_TRUE(true);
}

TEST(TypeRegistryEntry, ConstructCppComponent) {
    ui::StringInterner interner;
    bp2::TypeRegistry::Entry entry;
    entry.type_id = interner.intern("Battery");
    entry.iface = bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    });
    entry.description = "DC battery source";
    entry.is_blueprint = false;
    EXPECT_EQ(interner.resolve(entry.type_id), "Battery");
    EXPECT_FALSE(entry.is_blueprint);
    EXPECT_EQ(entry.iface.size(), 2u);
}

TEST(TypeRegistry, RegisterAndFind) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    auto bat_id = interner.intern("Battery");
    bp2::Interface iface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_component(bat_id, iface, "DC battery");

    EXPECT_TRUE(reg.has(bat_id));
    auto* entry = reg.find(bat_id);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->iface.size(), 2u);
    EXPECT_FALSE(entry->is_blueprint);
}

TEST(TypeRegistry, FindReturnsNullForMissing) {
    bp2::TypeRegistry reg;
    ui::StringInterner interner;
    EXPECT_EQ(reg.find(interner.intern("Nonexistent")), nullptr);
    EXPECT_FALSE(reg.has(interner.intern("Nonexistent")));
}

TEST(TypeRegistry, RegisterBlueprint) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    auto ps_id = interner.intern("power_system");
    bp2::Interface iface({
        {interner.intern("main_power"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("bus_28v"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_blueprint(ps_id, iface, "Power distribution");

    auto* entry = reg.find(ps_id);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->is_blueprint);
    EXPECT_EQ(entry->iface.size(), 2u);
}

TEST(TypeRegistry, InterfaceOfReturnsInterface) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    auto id = interner.intern("Resistor");
    bp2::Interface iface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_component(id, iface);

    bp2::Interface const& result = reg.interface_of(id);
    EXPECT_EQ(result.size(), 2u);
}

TEST(TypeRegistry, InterfaceOfThrowsForMissing) {
    bp2::TypeRegistry reg;
    ui::StringInterner interner;
    auto id = interner.intern("Nope");
    EXPECT_THROW(reg.interface_of(id), std::runtime_error);
}

TEST(TypeRegistry, OnMissingCallbackInvoked) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bool called = false;
    ui::InternedId missing_id;

    reg.set_on_missing([&](ui::InternedId id) {
        called = true;
        missing_id = id;
        reg.register_component(id, bp2::Interface(), "lazy-loaded");
    });

    auto id = interner.intern("LazyComponent");
    auto* entry = reg.find(id);
    EXPECT_EQ(entry, nullptr);
    EXPECT_FALSE(called);

    entry = reg.find_or_load(id);
    EXPECT_TRUE(called);
    ASSERT_NE(entry, nullptr);
}

TEST(TypeRegistry, OnMissingNotCalledWhenPresent) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bool called = false;
    reg.set_on_missing([&](ui::InternedId) { called = true; });

    auto id = interner.intern("X");
    reg.register_component(id, bp2::Interface());
    reg.find_or_load(id);
    EXPECT_FALSE(called);
}

TEST(TypeRegistry, SizeAndIteration) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    EXPECT_EQ(reg.size(), 0u);

    reg.register_component(interner.intern("A"), bp2::Interface());
    reg.register_component(interner.intern("B"), bp2::Interface());
    reg.register_blueprint(interner.intern("C"), bp2::Interface());
    EXPECT_EQ(reg.size(), 3u);

    size_t count = 0;
    for (auto const& [id, entry] : reg) {
        (void)id;
        (void)entry;
        ++count;
    }
    EXPECT_EQ(count, 3u);
}

TEST(TypeRegistry, TestFactoryHasBasicTypes) {
    ui::StringInterner interner;
    auto reg = bp2::TypeRegistry::create_test_registry(interner);
    EXPECT_TRUE(reg.has(interner.intern("Battery")));
    EXPECT_TRUE(reg.has(interner.intern("Resistor")));
    EXPECT_TRUE(reg.has(interner.intern("Ground")));
    EXPECT_GE(reg.size(), 3u);
}