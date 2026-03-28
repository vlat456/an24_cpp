#include <gtest/gtest.h>

#include "jit_solver/scheduler.h"
#include "jit_solver/state.h"

struct MockSource {
    int* counter = nullptr;
    int expected_order = 0;

    void execute(SimulationState& st, float dt) {
        (void)dt;
        EXPECT_EQ(*counter, expected_order) << "Source executed out of order";
        (*counter)++;
        st.values[0] = 28.0f;
    }
};

struct MockConsumer {
    int* counter = nullptr;
    int expected_order = 0;
    float received_voltage = 0.0f;

    void execute(SimulationState& st, float dt) {
        (void)dt;
        EXPECT_EQ(*counter, expected_order) << "Consumer executed out of order";
        (*counter)++;
        received_voltage = st.values[0];
    }
};

TEST(push_scheduler, SourcesBeforeConsumers) {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int counter = 0;
    MockSource src{&counter, 0};
    MockConsumer cons{&counter, 1};

    PushScheduler sched;
    sched.add_source(&src);
    sched.add_consumer(&cons);

    sched.step(st, 1.0f / 60.0f);

    EXPECT_EQ(counter, 2);
    EXPECT_FLOAT_EQ(cons.received_voltage, 28.0f);
}

TEST(push_scheduler, MultipleSourcesMultipleConsumers) {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int counter = 0;
    MockSource src1{&counter, 0};
    MockSource src2{&counter, 1};
    MockConsumer cons1{&counter, 2};
    MockConsumer cons2{&counter, 3};

    PushScheduler sched;
    sched.add_source(&src1);
    sched.add_source(&src2);
    sched.add_consumer(&cons1);
    sched.add_consumer(&cons2);

    sched.step(st, 1.0f / 60.0f);
    EXPECT_EQ(counter, 4);
}

TEST(push_scheduler, EmptySchedulerNoOp) {
    SimulationState st;
    PushScheduler sched;
    sched.step(st, 1.0f / 60.0f);
    EXPECT_EQ(sched.source_count(), 0u);
    EXPECT_EQ(sched.consumer_count(), 0u);
}

TEST(push_scheduler, SinglePassExecution) {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int exec_count = 0;
    MockSource src{&exec_count, 0};

    PushScheduler sched;
    sched.add_source(&src);

    sched.step(st, 1.0f / 60.0f);
    EXPECT_EQ(exec_count, 1);
}
