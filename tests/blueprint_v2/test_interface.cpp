#include <gtest/gtest.h>
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/interface/interface.h"
#include "ui/core/interned_id.h"

TEST(Direction, HasThreeValues) {
    EXPECT_NE(bp2::Direction::Input, bp2::Direction::Output);
    EXPECT_NE(bp2::Direction::Input, bp2::Direction::InOut);
    EXPECT_NE(bp2::Direction::Output, bp2::Direction::InOut);
}

TEST(PortDescriptor, ConstructAndAccess) {
    ui::StringInterner interner;
    auto name = interner.intern("v_out");
    bp2::PortDescriptor pd{name, Domain::Electrical, bp2::Direction::Output};
    EXPECT_EQ(pd.name, name);
    EXPECT_EQ(pd.domain, Domain::Electrical);
    EXPECT_EQ(pd.direction, bp2::Direction::Output);
}

TEST(PortDescriptor, EqualityByName) {
    ui::StringInterner interner;
    auto n1 = interner.intern("v_out");
    auto n2 = interner.intern("v_out");
    bp2::PortDescriptor a{n1, Domain::Electrical, bp2::Direction::Output};
    bp2::PortDescriptor b{n2, Domain::Electrical, bp2::Direction::Output};
    EXPECT_EQ(a, b);
}

TEST(PortDescriptor, InequalityByDomain) {
    ui::StringInterner interner;
    auto n = interner.intern("x");
    bp2::PortDescriptor a{n, Domain::Electrical, bp2::Direction::Output};
    bp2::PortDescriptor b{n, Domain::Logical, bp2::Direction::Output};
    EXPECT_NE(a, b);
}

TEST(Interface, EmptyByDefault) {
    bp2::Interface iface;
    EXPECT_EQ(iface.size(), 0u);
    EXPECT_EQ(iface.begin(), iface.end());
}

TEST(Interface, ConstructFromVector) {
    ui::StringInterner interner;
    std::vector<bp2::PortDescriptor> ports = {
        {interner.intern("a"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("b"), Domain::Electrical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);
    EXPECT_EQ(iface.size(), 2u);
}

TEST(Interface, IterationOrder) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
        {b, Domain::Electrical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);
    auto it = iface.begin();
    EXPECT_EQ(it->name, a);
    ++it;
    EXPECT_EQ(it->name, b);
}

TEST(Interface, FindByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    auto c = interner.intern("c");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
        {b, Domain::Logical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);

    auto found = iface.find(b);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->domain, Domain::Logical);

    EXPECT_FALSE(iface.find(c).has_value());
}

TEST(Interface, HasByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto z = interner.intern("z");
    bp2::Interface iface({{a, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_TRUE(iface.has(a));
    EXPECT_FALSE(iface.has(z));
}

TEST(Interface, AtByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    bp2::Interface iface({{a, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_EQ(iface.at(a).direction, bp2::Direction::Input);
}

TEST(Interface, EqualInterfaces) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
    };
    bp2::Interface i1(ports);
    bp2::Interface i2(ports);
    EXPECT_EQ(i1, i2);
}

TEST(Interface, UnequalInterfaces) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    bp2::Interface i1({{a, Domain::Electrical, bp2::Direction::Input}});
    bp2::Interface i2({{b, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_NE(i1, i2);
}