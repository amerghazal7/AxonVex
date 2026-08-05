/**
 * @file ros2AdapterSmokeTest.cpp
 * @brief C20 verification: ROS2Adapter's factory templates against real
 * rclcpp/std_msgs/std_srvs Humble headers, plus an rclcpp::init/Node smoke
 * check.
 *
 * Only built when the axonvex_ros2 plugin itself builds (rclcpp found) —
 * see the `if(TARGET axonvex_ros2)` guard in the root CMakeLists.txt. This
 * is a separate executable from test_core so BUILD_PLUGINS=OFF or a
 * missing rclcpp never breaks the main test binary.
 */

#include <axonvex_ros2/ros2Adapter.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <stdexcept>

using axonvex::ros2::ROS2Adapter;

namespace {

class Ros2AdapterSmokeTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        rclcpp::init(0, nullptr);
    }
    static void TearDownTestSuite() {
        rclcpp::shutdown();
    }
};

} // namespace

// Confirms rclcpp::init()/Node construction genuinely runs in this
// environment (no ROS daemon required — rmw/DDS discovery happens
// standalone). If this ever starts failing in CI, that is itself the
// signal the "smoke test infeasible here" note in the C20 report was wrong.
TEST_F(Ros2AdapterSmokeTest, NodeConstructionWorks) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_node");
    EXPECT_STREQ(node->get_name(), "axonvex_ros2_smoke_node");
}

TEST_F(Ros2AdapterSmokeTest, DefaultCastersAreRegisteredForStdString) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_defaultcasters");
    ROS2Adapter adapter(node);

    // std::string<->std_msgs::msg::String is one of the four default
    // casters (AXONVEX_ROS2_HAS_STD_MSGS): createSubscriber must not throw
    // "no type caster registered" for it without an explicit
    // registerTypeCaster() call.
    auto sub =
        adapter.createSubscriber<std::string, std_msgs::msg::String>("/axonvex_smoke/chatter");
    ASSERT_NE(sub, nullptr);

    auto pub =
        adapter.createPublisher<std::string, std_msgs::msg::String>("/axonvex_smoke/chatter_out");
    ASSERT_NE(pub, nullptr);
}

// C20: an unregistered (InternalType, RosMsg) pair must fail loudly, not
// silently no-op or dereference a null caster.
TEST_F(Ros2AdapterSmokeTest, MissingCasterThrowsInsteadOfSilentlyFailing) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_missingcaster");
    ROS2Adapter adapter(node);

    // int<->String was never registered (only float/double/bool/string are
    // default-registered).
    EXPECT_THROW((adapter.createSubscriber<int, std_msgs::msg::String>("/axonvex_smoke/unmapped")),
                 std::runtime_error);
    EXPECT_THROW(
        (adapter.createPublisher<int, std_msgs::msg::String>("/axonvex_smoke/unmapped_out")),
        std::runtime_error);
}

// C20 / review finding 3: createServer requires a request mapper — the
// request payload used to be silently discarded (`T data{}`), so a test
// that only constructs the server and never sends a request would still
// pass against that regression. This sends a REAL service request through
// a real rclcpp::Client, with a payload (`true`) that differs from a
// default-constructed T (`false`), and asserts the ServerUnit's sync
// output port received the MAPPED value — proving
// pushRequest(requestMapper(*request)) actually ran against the request
// that came off the wire, not a stand-in. ServerUnit has no response path
// (see ros2Adapter.hpp's createServer doc comment), so the client's
// response is a default-constructed SetBool::Response regardless; this
// test only asserts the request side, which is what C20 fixed.
TEST_F(Ros2AdapterSmokeTest, ServerMapsRequestThroughToServerUnit) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_server_roundtrip");
    ROS2Adapter adapter(node);

    auto server = adapter.createServer<bool, std_srvs::srv::SetBool>(
        "/axonvex_smoke/set_bool_roundtrip",
        [](const std_srvs::srv::SetBool::Request& req) { return req.data; });
    ASSERT_NE(server, nullptr);

    bool callbackFired = false;
    bool receivedValue = false;
    server->getOutput()->setOutputCallback([&](const bool& value) {
        callbackFired = true;
        receivedValue = value;
    });

    auto client = node->create_client<std_srvs::srv::SetBool>("/axonvex_smoke/set_bool_roundtrip");
    ASSERT_TRUE(client->wait_for_service(std::chrono::seconds(5)));

    auto request = std::make_shared<std_srvs::srv::SetBool::Request>();
    request->data = true; // distinctive: default-constructed bool is false

    auto future = client->async_send_request(request);

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    ASSERT_EQ(executor.spin_until_future_complete(future, std::chrono::seconds(5)),
              rclcpp::FutureReturnCode::SUCCESS);

    EXPECT_TRUE(callbackFired);
    EXPECT_TRUE(receivedValue);
}

TEST_F(Ros2AdapterSmokeTest, CreateServerRejectsNullRequestMapper) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_server_nullmapper");
    ROS2Adapter adapter(node);

    EXPECT_THROW(
        (adapter.createServer<bool, std_srvs::srv::SetBool>("/axonvex_smoke/set_bool_2", nullptr)),
        std::invalid_argument);
}

TEST_F(Ros2AdapterSmokeTest, CreateClientCompilesAndConstructs) {
    auto node = std::make_shared<rclcpp::Node>("axonvex_ros2_smoke_client");
    ROS2Adapter adapter(node);

    auto client =
        adapter.createClient<bool, std_srvs::srv::SetBool>("/axonvex_smoke/set_bool_client");
    ASSERT_NE(client, nullptr);
}
