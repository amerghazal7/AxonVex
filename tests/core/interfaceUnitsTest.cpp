#include <atomic>
#include <axonvex_core/interfaceUnits.hpp>
#include <gtest/gtest.h>
#include <string>

namespace {

using namespace axonvex::core;

// =========================================================================
// SubscriberUnit
// =========================================================================

TEST(SubscriberUnitTest, PushDataWritesToOutputPort) {
    SubscriberUnit<double> sub("test_sub");
    sub.initialize();

    class Consumer : public ProcessingUnit {
      public:
        Consumer() : ProcessingUnit("consumer") {
            in = createInputPort<double>(0, "in");
        }
        void processSync() override {}
        void processAsync() override {}
        void reset() override {}
        void initialize() override {}
        std::string getTypeDescription() override {
            return "Consumer";
        }
        InputPort<double>* in;
    };

    Consumer consumer;
    sub.getOutput()->connect(consumer.in);

    sub.pushData(42.0);
    EXPECT_TRUE(consumer.in->hasNewData());
    EXPECT_DOUBLE_EQ(consumer.in->read(), 42.0);
}

TEST(SubscriberUnitTest, TriggerFiresOnPushData) {
    SubscriberUnit<int> sub("trigger_test");
    sub.initialize();
    EXPECT_NE(sub.getTrigger(), nullptr);
    sub.pushData(7);
}

TEST(SubscriberUnitTest, ProcessSyncIsNoOp) {
    SubscriberUnit<float> sub("noop_test");
    sub.initialize();
    sub.processSync();
}

TEST(SubscriberUnitTest, TypeDescription) {
    SubscriberUnit<int> sub("desc_test");
    EXPECT_EQ(sub.getTypeDescription(), "SubscriberUnit");
}

// =========================================================================
// PublisherUnit
// =========================================================================

TEST(PublisherUnitTest, ProcessSyncCallsCallback) {
    PublisherUnit<double> pub("test_pub");
    pub.initialize();

    double published = 0;
    pub.setPublishCallback([&](const double& val) { published = val; });

    pub.getInput()->writeData(99.5);
    pub.processSync();
    EXPECT_DOUBLE_EQ(published, 99.5);
}

TEST(PublisherUnitTest, NoCallbackDoesNotCrash) {
    PublisherUnit<int> pub("safe_pub");
    pub.initialize();
    pub.getInput()->writeData(5);
    pub.processSync();
}

TEST(PublisherUnitTest, AsyncInputPublishes) {
    PublisherUnit<std::string> pub("async_pub");
    pub.initialize();

    std::string published;
    pub.setPublishCallback([&](const std::string& val) { published = val; });

    pub.getAsyncInput()->update("hello");
    pub.processAsync();
    EXPECT_EQ(published, "hello");
}

TEST(PublisherUnitTest, TypeDescription) {
    PublisherUnit<int> pub("desc_test");
    EXPECT_EQ(pub.getTypeDescription(), "PublisherUnit");
}

// =========================================================================
// ServerUnit
// =========================================================================

TEST(ServerUnitTest, PushRequestWritesToBothPorts) {
    ServerUnit<bool> srv("test_srv");
    srv.initialize();

    srv.pushRequest(true);

    EXPECT_NE(srv.getAsyncOutput(), nullptr);
    EXPECT_NE(srv.getOutput(), nullptr);
}

TEST(ServerUnitTest, TypeDescription) {
    ServerUnit<int> srv("desc_test");
    EXPECT_EQ(srv.getTypeDescription(), "ServerUnit");
}

// =========================================================================
// ClientUnit
// =========================================================================

TEST(ClientUnitTest, ProcessAsyncCallsRequestCallback) {
    ClientUnit<int> cli("test_cli");
    cli.initialize();

    int requested = 0;
    cli.setRequestCallback([&](const int& val) { requested = val; });

    cli.getAsyncInput()->update(42);
    cli.processAsync();
    EXPECT_EQ(requested, 42);
}

TEST(ClientUnitTest, FinishedOutputFiresAfterRequest) {
    ClientUnit<int> cli("finished_test");
    cli.initialize();
    cli.setRequestCallback([](const int&) {});

    cli.getAsyncInput()->update(1);
    cli.processAsync();
    EXPECT_NE(cli.getFinishedOutput(), nullptr);
}

TEST(ClientUnitTest, TypeDescription) {
    ClientUnit<int> cli("desc_test");
    EXPECT_EQ(cli.getTypeDescription(), "ClientUnit");
}

} // namespace
