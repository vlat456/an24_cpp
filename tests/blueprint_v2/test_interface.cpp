#include <gtest/gtest.h>
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/type_definition_interface.h"
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

TEST(TypeDefinitionInterface, InterfaceFromTypeDefinitionCoversAllPortTypes) {
    ui::StringInterner interner;

    TypeDefinition def;
    def.classname = "AllPortTypes";
    def.ports["v"] = Port{bp2::Direction::Input, PortType::V};
    def.ports["i"] = Port{bp2::Direction::Output, PortType::I};
    def.ports["b"] = Port{bp2::Direction::Input, PortType::Bool};
    def.ports["rpm"] = Port{bp2::Direction::Output, PortType::RPM};
    def.ports["tmp"] = Port{bp2::Direction::InOut, PortType::Temperature};
    def.ports["prs"] = Port{bp2::Direction::InOut, PortType::Pressure};
    def.ports["pos"] = Port{bp2::Direction::Input, PortType::Position};
    def.ports["any"] = Port{bp2::Direction::Output, PortType::Any};

    bp2::Interface iface = bp2::interface_from_type_definition(def, interner);

    ASSERT_EQ(iface.size(), 8u);

    auto v = iface.find(interner.intern("v"));
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->domain, Domain::Electrical);
    EXPECT_EQ(v->direction, bp2::Direction::Input);

    auto i = iface.find(interner.intern("i"));
    ASSERT_TRUE(i.has_value());
    EXPECT_EQ(i->domain, Domain::Electrical);
    EXPECT_EQ(i->direction, bp2::Direction::Output);

    auto b = iface.find(interner.intern("b"));
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->domain, Domain::Logical);

    auto rpm = iface.find(interner.intern("rpm"));
    ASSERT_TRUE(rpm.has_value());
    EXPECT_EQ(rpm->domain, Domain::Mechanical);

    auto tmp = iface.find(interner.intern("tmp"));
    ASSERT_TRUE(tmp.has_value());
    EXPECT_EQ(tmp->domain, Domain::Thermal);
    EXPECT_EQ(tmp->direction, bp2::Direction::InOut);

    auto prs = iface.find(interner.intern("prs"));
    ASSERT_TRUE(prs.has_value());
    EXPECT_EQ(prs->domain, Domain::Hydraulic);

    auto pos = iface.find(interner.intern("pos"));
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->domain, Domain::Mechanical);

    auto any = iface.find(interner.intern("any"));
    ASSERT_TRUE(any.has_value());
    EXPECT_EQ(any->domain, Domain::Electrical);
}
