#include <axonvex_adapters/adapterInterface.hpp>
#include <atomic>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

class MessageSink final : public axonvex::adapters::AdapterInterface::MessageCallback {
  public:
    void callbackPerform(const axonvex::adapters::AdapterMessage data) override {
        ++count;
        lastTopic = data.topic;
        lastPayload = data.payload;
    }
    std::atomic<int> count{0};
    std::string lastTopic;
    std::vector<uint8_t> lastPayload;
};

class ErrorSink final : public axonvex::adapters::AdapterInterface::ErrorCallback {
  public:
    void callbackPerform(const std::string data) override {
        ++count;
        lastError = data;
    }
    std::atomic<int> count{0};
    std::string lastError;
};

// =========================================================================
// Adapter contract tests using MockAdapter
// =========================================================================

class AdapterContractTest : public ::testing::Test {
  protected:
    axonvex::adapters::MockAdapter adapter;
};

TEST_F(AdapterContractTest, InitialStateIsDisconnected) {
    EXPECT_EQ(adapter.state(), axonvex::adapters::AdapterState::Disconnected);
    EXPECT_FALSE(adapter.isConnected());
}

TEST_F(AdapterContractTest, LifecycleHappyPath) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_EQ(adapter.state(), axonvex::adapters::AdapterState::Disconnected);

    EXPECT_TRUE(adapter.start());
    EXPECT_EQ(adapter.state(), axonvex::adapters::AdapterState::Connected);
    EXPECT_TRUE(adapter.isConnected());

    EXPECT_TRUE(adapter.stop());
    EXPECT_EQ(adapter.state(), axonvex::adapters::AdapterState::Disconnected);
    EXPECT_FALSE(adapter.isConnected());
}

TEST_F(AdapterContractTest, StartBeforeInitializeFails) {
    ErrorSink errSink;
    adapter.registerErrorHandler("default", &errSink);
    EXPECT_FALSE(adapter.start());
    EXPECT_GT(errSink.count.load(), 0);
}

TEST_F(AdapterContractTest, DoubleInitializeInConnectedStateFails) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());
    EXPECT_FALSE(adapter.initialize());
}

TEST_F(AdapterContractTest, StopIsIdempotent) {
    EXPECT_TRUE(adapter.stop());
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());
    EXPECT_TRUE(adapter.stop());
    EXPECT_TRUE(adapter.stop());
}

TEST_F(AdapterContractTest, ShutdownReleasesResources) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());
    adapter.shutdown();
    EXPECT_EQ(adapter.state(), axonvex::adapters::AdapterState::Disconnected);
    EXPECT_FALSE(adapter.isConnected());
}

TEST_F(AdapterContractTest, PublishWhileDisconnectedFails) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_FALSE(adapter.publish("test_topic", {0x01, 0x02}));
}

TEST_F(AdapterContractTest, PublishAndSubscribeRouting) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());

    MessageSink sink;
    adapter.subscribe("sensor/imu", &sink);

    const std::vector<uint8_t> payload{10, 20, 30};
    EXPECT_TRUE(adapter.publish("sensor/imu", payload));

    EXPECT_EQ(sink.count.load(), 1);
    EXPECT_EQ(sink.lastTopic, "sensor/imu");
    EXPECT_EQ(sink.lastPayload, payload);
}

TEST_F(AdapterContractTest, MultipleSubscribersReceiveMessages) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());

    MessageSink sink1, sink2;
    adapter.subscribe("nav/pose", &sink1);
    adapter.subscribe("nav/pose", &sink2);

    EXPECT_TRUE(adapter.publish("nav/pose", {0xAA}));
    EXPECT_EQ(sink1.count.load(), 1);
    EXPECT_EQ(sink2.count.load(), 1);
}

TEST_F(AdapterContractTest, UnsubscribeStopsDelivery) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());

    MessageSink sink;
    adapter.subscribe("topic_a", &sink);
    EXPECT_TRUE(adapter.publish("topic_a", {1}));
    EXPECT_EQ(sink.count.load(), 1);

    EXPECT_TRUE(adapter.unsubscribe("topic_a", &sink));
    EXPECT_TRUE(adapter.publish("topic_a", {2}));
    EXPECT_EQ(sink.count.load(), 1);
}

TEST_F(AdapterContractTest, TopicIsolation) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());

    MessageSink sinkA, sinkB;
    adapter.subscribe("topic_a", &sinkA);
    adapter.subscribe("topic_b", &sinkB);

    EXPECT_TRUE(adapter.publish("topic_a", {1}));
    EXPECT_EQ(sinkA.count.load(), 1);
    EXPECT_EQ(sinkB.count.load(), 0);
}

TEST_F(AdapterContractTest, StatisticsTrackMessagesAndBytes) {
    EXPECT_TRUE(adapter.initialize());
    EXPECT_TRUE(adapter.start());

    const std::vector<uint8_t> payload{1, 2, 3, 4, 5};
    EXPECT_TRUE(adapter.publish("stats_topic", payload));
    EXPECT_TRUE(adapter.publish("stats_topic", payload));

    auto stats = adapter.statistics();
    EXPECT_EQ(stats.messagesSent, 2u);
    EXPECT_EQ(stats.bytesSent, 10u);
}

TEST_F(AdapterContractTest, StatisticsTrackErrors) {
    auto before = adapter.statistics().errorsCount;
    adapter.publish("fail_topic", {1});
    EXPECT_GT(adapter.statistics().errorsCount, before);
}

TEST_F(AdapterContractTest, HealthCheckReflectsConnectionState) {
    EXPECT_FALSE(adapter.healthCheck());
    EXPECT_TRUE(adapter.initialize());
    EXPECT_FALSE(adapter.healthCheck());
    EXPECT_TRUE(adapter.start());
    EXPECT_TRUE(adapter.healthCheck());
    EXPECT_TRUE(adapter.stop());
    EXPECT_FALSE(adapter.healthCheck());
}

TEST_F(AdapterContractTest, NameAndProtocolIdAreNonEmpty) {
    EXPECT_FALSE(adapter.name().empty());
    EXPECT_FALSE(adapter.protocolId().empty());
}

TEST_F(AdapterContractTest, ConfigureUnknownKeyReturnsFalse) {
    EXPECT_FALSE(adapter.configure("nonexistent_key", "value"));
}

TEST_F(AdapterContractTest, SubscriberCountTracking) {
    MessageSink s1, s2;
    EXPECT_EQ(adapter.subscriberCount("topic"), 0u);
    adapter.subscribe("topic", &s1);
    EXPECT_EQ(adapter.subscriberCount("topic"), 1u);
    adapter.subscribe("topic", &s2);
    EXPECT_EQ(adapter.subscriberCount("topic"), 2u);
    adapter.unsubscribe("topic", &s1);
    EXPECT_EQ(adapter.subscriberCount("topic"), 1u);
}

} // namespace
