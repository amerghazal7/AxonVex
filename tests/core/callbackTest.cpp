/**
 * @file callbackTest.cpp
 * @brief Unit tests for the AxonVex callback system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/callback.hpp>
#include <axonvex/core/caller.hpp>
#include <axonvex/core/callerKeyed.hpp>
#include <string>
#include <vector>
#include <stdexcept>

namespace axonvex::core::test {

// Test callback implementations
class TestCallback : public Callback<int> {
public:
    std::vector<int> received_data;

    void callbackPerform(const int data) override {
        received_data.push_back(data);
    }

    void clear() {
        received_data.clear();
    }
};

class ExceptionCallback : public Callback<int> {
public:
    void callbackPerform(const int data) override {
        throw std::runtime_error("Test exception");
    }
};

class StringCallback : public Callback<std::string> {
public:
    std::vector<std::string> received_data;

    void callbackPerform(const std::string data) override {
        received_data.push_back(data);
    }

    void clear() {
        received_data.clear();
    }
};

// Tests for basic Caller functionality
class CallerTest : public ::testing::Test {
protected:
    void SetUp() override {
        callback1.clear();
        callback2.clear();
        callback3.clear();
    }

    TestCallback callback1;
    TestCallback callback2;
    TestCallback callback3;
};

TEST_F(CallerTest, ConstructorAndDestructor) {
    Caller<int> caller;
    EXPECT_EQ(caller.getCallbackCount(), 0);
    EXPECT_TRUE(caller.empty());
}

TEST_F(CallerTest, RegisterSingleCallback) {
    Caller<int> caller;

    caller.registerCallback(&callback1);
    EXPECT_EQ(caller.getCallbackCount(), 1);
    EXPECT_FALSE(caller.empty());
}

TEST_F(CallerTest, RegisterMultipleCallbacks) {
    Caller<int> caller;

    caller.registerCallback(&callback1);
    caller.registerCallback(&callback2);
    caller.registerCallback(&callback3);

    EXPECT_EQ(caller.getCallbackCount(), 3);
    EXPECT_FALSE(caller.empty());
}

TEST_F(CallerTest, RegisterNullCallback) {
    Caller<int> caller;

    caller.registerCallback(nullptr);
    EXPECT_EQ(caller.getCallbackCount(), 0);
    EXPECT_TRUE(caller.empty());
}

TEST_F(CallerTest, CallSingleCallback) {
    Caller<int> caller;
    caller.registerCallback(&callback1);

    caller.callCallbacks(42);

    ASSERT_EQ(callback1.received_data.size(), 1);
    EXPECT_EQ(callback1.received_data[0], 42);
}

TEST_F(CallerTest, CallMultipleCallbacks) {
    Caller<int> caller;
    caller.registerCallback(&callback1);
    caller.registerCallback(&callback2);

    caller.callCallbacks(123);

    ASSERT_EQ(callback1.received_data.size(), 1);
    EXPECT_EQ(callback1.received_data[0], 123);

    ASSERT_EQ(callback2.received_data.size(), 1);
    EXPECT_EQ(callback2.received_data[0], 123);
}

TEST_F(CallerTest, CallNoCallbacks) {
    Caller<int> caller;

    // Should not crash
    caller.callCallbacks(42);
    SUCCEED();
}

TEST_F(CallerTest, UnregisterCallback) {
    Caller<int> caller;
    caller.registerCallback(&callback1);
    caller.registerCallback(&callback2);

    EXPECT_TRUE(caller.unregisterCallback(&callback1));
    EXPECT_EQ(caller.getCallbackCount(), 1);

    caller.callCallbacks(42);
    EXPECT_EQ(callback1.received_data.size(), 0);
    ASSERT_EQ(callback2.received_data.size(), 1);
    EXPECT_EQ(callback2.received_data[0], 42);
}

TEST_F(CallerTest, UnregisterNonExistentCallback) {
    Caller<int> caller;
    caller.registerCallback(&callback1);

    EXPECT_FALSE(caller.unregisterCallback(&callback2));
    EXPECT_EQ(caller.getCallbackCount(), 1);
}

TEST_F(CallerTest, UnregisterAllCallback) {
    Caller<int> caller;
    caller.registerCallback(&callback1);
    caller.registerCallback(&callback1);  // Register same callback twice
    caller.registerCallback(&callback2);

    EXPECT_EQ(caller.unregisterAllCallback(&callback1), 2);
    EXPECT_EQ(caller.getCallbackCount(), 1);
}

TEST_F(CallerTest, Clear) {
    Caller<int> caller;
    caller.registerCallback(&callback1);
    caller.registerCallback(&callback2);

    caller.clear();
    EXPECT_EQ(caller.getCallbackCount(), 0);
    EXPECT_TRUE(caller.empty());
}

TEST_F(CallerTest, ExceptionSafety) {
    Caller<int> caller;
    ExceptionCallback exceptionCallback;

    caller.registerCallback(&callback1);
    caller.registerCallback(&exceptionCallback);
    caller.registerCallback(&callback2);

    // Should catch the exception and return the count
    size_t exceptions = caller.callCallbacksSafe(42);
    EXPECT_EQ(exceptions, 1);

    // Other callbacks should still have been called
    ASSERT_EQ(callback1.received_data.size(), 1);
    EXPECT_EQ(callback1.received_data[0], 42);
    ASSERT_EQ(callback2.received_data.size(), 1);
    EXPECT_EQ(callback2.received_data[0], 42);
}

// Tests for CallerKeyed functionality
class CallerKeyedTest : public ::testing::Test {
protected:
    void SetUp() override {
        stringCallback1.clear();
        stringCallback2.clear();
        intCallback1.clear();
        intCallback2.clear();
    }

    StringCallback stringCallback1;
    StringCallback stringCallback2;
    TestCallback intCallback1;
    TestCallback intCallback2;
};

TEST_F(CallerKeyedTest, ConstructorAndDestructor) {
    CallerKeyed<std::string, int> caller;
    EXPECT_EQ(caller.getTotalCallbackCount(), 0);
    EXPECT_TRUE(caller.empty());
}

TEST_F(CallerKeyedTest, RegisterKeyedCallback) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", &intCallback1);
    EXPECT_EQ(caller.getTotalCallbackCount(), 1);
    EXPECT_EQ(caller.getCallbackCountForKey("test"), 1);
    EXPECT_TRUE(caller.hasCallbacksForKey("test"));
    EXPECT_FALSE(caller.empty());
}

TEST_F(CallerKeyedTest, RegisterMultipleCallbacksSameKey) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", &intCallback1);
    caller.registerKeyedCallback("test", &intCallback2);

    EXPECT_EQ(caller.getTotalCallbackCount(), 2);
    EXPECT_EQ(caller.getCallbackCountForKey("test"), 2);
}

TEST_F(CallerKeyedTest, RegisterCallbacksDifferentKeys) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &intCallback2);

    EXPECT_EQ(caller.getTotalCallbackCount(), 2);
    EXPECT_EQ(caller.getCallbackCountForKey("key1"), 1);
    EXPECT_EQ(caller.getCallbackCountForKey("key2"), 1);
    EXPECT_TRUE(caller.hasCallbacksForKey("key1"));
    EXPECT_TRUE(caller.hasCallbacksForKey("key2"));
    EXPECT_FALSE(caller.hasCallbacksForKey("nonexistent"));
}

TEST_F(CallerKeyedTest, RegisterNullCallback) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", nullptr);
    EXPECT_EQ(caller.getTotalCallbackCount(), 0);
    EXPECT_FALSE(caller.hasCallbacksForKey("test"));
}

TEST_F(CallerKeyedTest, CallCallbacksByKey) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &intCallback2);

    caller.callCallbacksByKey("key1", 42);

    ASSERT_EQ(intCallback1.received_data.size(), 1);
    EXPECT_EQ(intCallback1.received_data[0], 42);
    EXPECT_EQ(intCallback2.received_data.size(), 0);  // Should not be called
}

TEST_F(CallerKeyedTest, CallMultipleCallbacksSameKey) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", &intCallback1);
    caller.registerKeyedCallback("test", &intCallback2);

    caller.callCallbacksByKey("test", 123);

    ASSERT_EQ(intCallback1.received_data.size(), 1);
    EXPECT_EQ(intCallback1.received_data[0], 123);
    ASSERT_EQ(intCallback2.received_data.size(), 1);
    EXPECT_EQ(intCallback2.received_data[0], 123);
}

TEST_F(CallerKeyedTest, CallCallbacksNonExistentKey) {
    CallerKeyed<std::string, int> caller;
    caller.registerKeyedCallback("test", &intCallback1);

    // Should not crash
    caller.callCallbacksByKey("nonexistent", 42);
    EXPECT_EQ(intCallback1.received_data.size(), 0);
}

TEST_F(CallerKeyedTest, CallAllCallbacks) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &intCallback2);

    caller.callAllCallbacks(999);

    ASSERT_EQ(intCallback1.received_data.size(), 1);
    EXPECT_EQ(intCallback1.received_data[0], 999);
    ASSERT_EQ(intCallback2.received_data.size(), 1);
    EXPECT_EQ(intCallback2.received_data[0], 999);
}

TEST_F(CallerKeyedTest, UnregisterKeyedCallback) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", &intCallback1);
    caller.registerKeyedCallback("test", &intCallback2);

    EXPECT_TRUE(caller.unregisterKeyedCallback("test", &intCallback1));
    EXPECT_EQ(caller.getCallbackCountForKey("test"), 1);

    caller.callCallbacksByKey("test", 42);
    EXPECT_EQ(intCallback1.received_data.size(), 0);
    ASSERT_EQ(intCallback2.received_data.size(), 1);
    EXPECT_EQ(intCallback2.received_data[0], 42);
}

TEST_F(CallerKeyedTest, UnregisterNonExistentKeyedCallback) {
    CallerKeyed<std::string, int> caller;
    caller.registerKeyedCallback("test", &intCallback1);

    EXPECT_FALSE(caller.unregisterKeyedCallback("test", &intCallback2));
    EXPECT_FALSE(caller.unregisterKeyedCallback("nonexistent", &intCallback1));
}

TEST_F(CallerKeyedTest, UnregisterAllCallbacksForKey) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("test", &intCallback1);
    caller.registerKeyedCallback("test", &intCallback2);
    caller.registerKeyedCallback("other", &intCallback1);

    EXPECT_EQ(caller.unregisterAllCallbacksForKey("test"), 2);
    EXPECT_EQ(caller.getTotalCallbackCount(), 1);
    EXPECT_FALSE(caller.hasCallbacksForKey("test"));
    EXPECT_TRUE(caller.hasCallbacksForKey("other"));
}

TEST_F(CallerKeyedTest, UnregisterAllCallback) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &intCallback1);
    caller.registerKeyedCallback("key1", &intCallback2);

    EXPECT_EQ(caller.unregisterAllCallback(&intCallback1), 2);
    EXPECT_EQ(caller.getTotalCallbackCount(), 1);
}

TEST_F(CallerKeyedTest, GetAllKeys) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("zebra", &intCallback1);
    caller.registerKeyedCallback("alpha", &intCallback2);
    caller.registerKeyedCallback("beta", &intCallback1);

    auto keys = caller.getAllKeys();
    EXPECT_EQ(keys.size(), 3);

    // Keys should be sorted (multimap property)
    EXPECT_EQ(keys[0], "alpha");
    EXPECT_EQ(keys[1], "beta");
    EXPECT_EQ(keys[2], "zebra");
}

TEST_F(CallerKeyedTest, NumericKeys) {
    CallerKeyed<int, std::string> caller;

    caller.registerKeyedCallback(100, &stringCallback1);
    caller.registerKeyedCallback(200, &stringCallback2);
    caller.registerKeyedCallback(100, &stringCallback2);

    caller.callCallbacksByKey(100, "message for 100");

    ASSERT_EQ(stringCallback1.received_data.size(), 1);
    EXPECT_EQ(stringCallback1.received_data[0], "message for 100");
    ASSERT_EQ(stringCallback2.received_data.size(), 1);
    EXPECT_EQ(stringCallback2.received_data[0], "message for 100");
}

TEST_F(CallerKeyedTest, ExceptionSafety) {
    CallerKeyed<std::string, int> caller;
    ExceptionCallback exceptionCallback;

    caller.registerKeyedCallback("test", &intCallback1);
    caller.registerKeyedCallback("test", &exceptionCallback);
    caller.registerKeyedCallback("test", &intCallback2);

    size_t exceptions = caller.callCallbacksByKeySafe("test", 42);
    EXPECT_EQ(exceptions, 1);

    // Other callbacks should still have been called
    ASSERT_EQ(intCallback1.received_data.size(), 1);
    EXPECT_EQ(intCallback1.received_data[0], 42);
    ASSERT_EQ(intCallback2.received_data.size(), 1);
    EXPECT_EQ(intCallback2.received_data[0], 42);
}

TEST_F(CallerKeyedTest, ExceptionSafetyAllCallbacks) {
    CallerKeyed<std::string, int> caller;
    ExceptionCallback exceptionCallback;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &exceptionCallback);
    caller.registerKeyedCallback("key3", &intCallback2);

    size_t exceptions = caller.callAllCallbacksSafe(42);
    EXPECT_EQ(exceptions, 1);

    // Other callbacks should still have been called
    ASSERT_EQ(intCallback1.received_data.size(), 1);
    EXPECT_EQ(intCallback1.received_data[0], 42);
    ASSERT_EQ(intCallback2.received_data.size(), 1);
    EXPECT_EQ(intCallback2.received_data[0], 42);
}

TEST_F(CallerKeyedTest, Clear) {
    CallerKeyed<std::string, int> caller;

    caller.registerKeyedCallback("key1", &intCallback1);
    caller.registerKeyedCallback("key2", &intCallback2);

    caller.clear();
    EXPECT_EQ(caller.getTotalCallbackCount(), 0);
    EXPECT_TRUE(caller.empty());
    EXPECT_FALSE(caller.hasCallbacksForKey("key1"));
    EXPECT_FALSE(caller.hasCallbacksForKey("key2"));
}

} // namespace axonvex::core::test
